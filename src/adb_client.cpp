#include "adb_client.h"
#include "adb_resource.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sstream>
// ========== 内部：adb 提取（懒加载，整个进程只做一次） ==========

namespace {

bool file_of_size(const std::string& path, unsigned int want) {
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fad)) return false;
    return fad.nFileSizeHigh == 0 && fad.nFileSizeLow == want;
}

std::string exe_dir() {
    char buf[MAX_PATH] = {0};
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    std::string p(buf, n);
    auto pos = p.find_last_of("\\/");
    return pos == std::string::npos ? std::string{} : p.substr(0, pos);
}

// adb 启动守护进程时会先在临时目录里开 adb.log，开不了就 "failed to start daemon"。
// 有的机器 %TEMP% 对当前用户不可写（策略 / 安全软件 / ACL 损坏），
// 所以这里先找一个确实能写的地方，再把子进程的 TEMP/TMP 指过去。
std::string prepare_tmp_dir() {
    std::vector<std::string> cands;
    if (const char* env = std::getenv("CRBOT_TMP"); env && *env) {
        cands.push_back(env);
    }
    const std::string base = exe_dir();
    if (!base.empty()) cands.push_back(base + "\\tmp");
    if (const char* local = std::getenv("LOCALAPPDATA"); local && *local) {
        cands.push_back(std::string(local) + "\\CRBot\\tmp");
    }

    for (const auto& dir : cands) {
        CreateDirectoryA(dir.c_str(), nullptr);
        const std::string probe = dir + "\\.write_test";
        {
            std::ofstream t(probe, std::ios::binary | std::ios::trunc);
            if (!t) continue;
            t.write("ok", 2);
        }
        // 真的写成功了吗（写完再查大小，不要信 ofstream 的状态）
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        const bool ok = GetFileAttributesExA(probe.c_str(), GetFileExInfoStandard, &fad) &&
                        fad.nFileSizeLow == 2;
        DeleteFileA(probe.c_str());
        if (!ok) continue;

        SetEnvironmentVariableA("TEMP", dir.c_str());
        SetEnvironmentVariableA("TMP", dir.c_str());
        return dir;
    }
    return {};  // 一个都不行就保持原样
}

}  // namespace

// 找一份可用的 adb。优先级：
//   0. %CRBOT_ADB% 环境变量（手动指定，调试用）
//   1. 已经解出来的副本（大小对得上就复用）—— adb 守护进程会锁住自己的镜像文件，
//      重复 truncate 会失败
//   2. 往 %LOCALAPPDATA%\CRBot\ 或 %TEMP% 落一份
//   3. 退回 exe 旁边 / 工程根目录下的 tools\adb.exe
// 第 3 步很重要：某些机器上杀软/组策略不允许往 TEMP 里写 exe，
// 这时用仓库里现成的 adb 反而最稳。
static std::string extract_adb() {
    const char* kName = "crbot_adb.exe";

    if (const char* env = std::getenv("CRBOT_ADB"); env && *env) {
        if (GetFileAttributesA(env) != INVALID_FILE_ATTRIBUTES) return env;
    }

    std::vector<std::string> dirs;
    if (const char* local = std::getenv("LOCALAPPDATA"); local && *local) {
        dirs.push_back(std::string(local) + "\\CRBot");
    }
    char tempPath[MAX_PATH] = {0};
    if (GetTempPathA(MAX_PATH, tempPath) != 0) {
        std::string t(tempPath);
        while (!t.empty() && (t.back() == '\\' || t.back() == '/')) t.pop_back();
        dirs.push_back(t);
    }

    // 1. 先复用已有副本（不碰文件，避免锁冲突）
    for (const auto& dir : dirs) {
        std::string path = dir + "\\" + kName;
        if (file_of_size(path, adb_size)) return path;
    }

    // 2. 再尝试落盘
    std::string tried;
    for (const auto& dir : dirs) {
        std::string path = dir + "\\" + kName;
        CreateDirectoryA(dir.c_str(), nullptr);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (out) {
            out.write(reinterpret_cast<const char*>(adb_data), adb_size);
            out.close();
            if (file_of_size(path, adb_size)) return path;
        }
        tried += " " + path;
    }

    // 3. 退回磁盘上现成的 adb
    const std::string base = exe_dir();
    for (const std::string& rel : {"\\tools\\adb.exe", "\\..\\tools\\adb.exe"}) {
        std::string cand = base + rel;
        if (!base.empty() && GetFileAttributesA(cand.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return cand;
        }
    }

    throw std::runtime_error("Cannot extract adb (tried:" + tried +
                             "), and no tools\\adb.exe next to the executable");
}

std::string AdbClient::get_adb_path() {
    static std::string path = [] {
        const std::string tmp = prepare_tmp_dir();
        if (!tmp.empty()) std::cout << "[ADB] tmp=" << tmp << std::endl;
        std::string p = extract_adb();
        std::cout << "[ADB] using " << p << std::endl;
        return p;
    }();
    return path;
}

// ========== 进程执行辅助 ==========

static std::string build_cmd_line(const std::string& adb_path,
                                  const std::string& serial,
                                  const std::string& args) {
    // 格式: "adb.exe" -s <serial> <args>
    if (serial.empty()) {
        return "\"" + adb_path + "\" " + args;
    }
    return "\"" + adb_path + "\" -s " + serial + " " + args;
}

struct ProcessResult {
    std::vector<uint8_t> out;
    DWORD exit_code = 0;
    bool timed_out = false;
};

// 关键：不能等管道 EOF。
// adb 的守护进程是 adb 客户端 fork 出来的，会继承我们这个管道的写端；
// 客户端退出后管道也不会断，ReadFile 就永远阻塞 —— 表现就是 “卡在这没反应”。
// 所以改成：轮询可读字节 + 只等【直接子进程】退出，再加一个总超时。
static ProcessResult run_process_ex(const std::string& cmd_line, int timeout_ms,
                                    AdbCancelState* cancel = nullptr) {
    ProcessResult res;

    if (cancel) {
        std::lock_guard<std::mutex> lk(cancel->mtx);
        if (cancel->cancelled) throw std::runtime_error("adb command cancelled");
    }

    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE h_read = nullptr, h_write = nullptr;
    if (!CreatePipe(&h_read, &h_write, &sa, 0)) {
        throw std::runtime_error("CreatePipe failed");
    }
    SetHandleInformation(h_read, HANDLE_FLAG_INHERIT, 0);

    PROCESS_INFORMATION pi = {};
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = h_write;
    si.hStdError  = h_write;

    // cmd_line 需要可修改 — 复制到非 const buffer
    std::vector<char> cmd_buf(cmd_line.begin(), cmd_line.end());
    cmd_buf.push_back('\0');

    BOOL ok = CreateProcessA(
        nullptr, cmd_buf.data(),
        nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW,
        nullptr, nullptr,
        &si, &pi
    );

    CloseHandle(h_write);

    if (!ok) {
        CloseHandle(h_read);
        throw std::runtime_error("CreateProcess failed for: " + cmd_line);
    }

    // 登记子进程句柄，别的线程可以据此打断
    if (cancel) {
        std::lock_guard<std::mutex> lk(cancel->mtx);
        cancel->proc = pi.hProcess;
        if (cancel->cancelled) TerminateProcess(pi.hProcess, 1);
    }

    const ULONGLONG start = GetTickCount64();
    bool child_done = false;

    for (;;) {
        DWORD avail = 0;
        if (PeekNamedPipe(h_read, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            uint8_t buf[8192];
            DWORD want = avail < sizeof(buf) ? avail : static_cast<DWORD>(sizeof(buf));
            DWORD got = 0;
            if (ReadFile(h_read, buf, want, &got, nullptr) && got > 0) {
                res.out.insert(res.out.end(), buf, buf + got);
                continue;
            }
        }

        if (child_done) {
            // 子进程已经退出，再确认管道里没有残留就可以收工
            DWORD left = 0;
            bool more = PeekNamedPipe(h_read, nullptr, 0, nullptr, &left, nullptr) && left > 0;
            if (!more) break;
            continue;
        }

        DWORD w = WaitForSingleObject(pi.hProcess, 20);
        if (w == WAIT_OBJECT_0) {
            child_done = true;
        } else if (GetTickCount64() - start > static_cast<ULONGLONG>(timeout_ms)) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 1000);
            res.timed_out = true;
            child_done = true;
        }
    }

    GetExitCodeProcess(pi.hProcess, &res.exit_code);

    // 先注销句柄再关，避免另一个线程 cancel() 打到已关闭的句柄
    if (cancel) {
        std::lock_guard<std::mutex> lk(cancel->mtx);
        cancel->proc = nullptr;
    }

    CloseHandle(h_read);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return res;
}

static std::string run_process(const std::string& cmd_line,
                               AdbCancelState* cancel = nullptr) {
    ProcessResult r = run_process_ex(cmd_line, 60000, cancel);
    std::string output;
    if (!r.out.empty()) {
        output.assign(reinterpret_cast<const char*>(r.out.data()), r.out.size());
    }
    if (r.timed_out) {
        throw std::runtime_error("adb command timed out: " + cmd_line + "\n" + output);
    }
    if (r.exit_code != 0) {
        throw std::runtime_error(
            "adb command failed (exit " + std::to_string(r.exit_code) + "): " + cmd_line +
            "\n" + output);
    }
    return output;
}

static std::vector<uint8_t> run_process_binary(const std::string& cmd_line,
                                               AdbCancelState* cancel = nullptr) {
    ProcessResult r = run_process_ex(cmd_line, 60000, cancel);
    if (r.timed_out) {
        throw std::runtime_error("adb command timed out: " + cmd_line);
    }
    if (r.exit_code != 0) {
        throw std::runtime_error(
            "adb command failed (exit " + std::to_string(r.exit_code) + "): " + cmd_line);
    }
    return r.out;
}

// ========== 常驻 adb shell 会话 ==========
//
// 每次 `adb shell input tap` 都要新起一个 adb 进程（~150ms 起步）。
// 出一次牌要两次 tap，加上截图，单帧就上到 1.4 秒。
// 这里保持一个 `adb -s X shell` 进程，命令写到它的 stdin，
// 用哨兵标记判断一条命令是否执行完（无 tty 时设备端不会回显）。

class AdbSession {
public:
    AdbSession(std::string adb, std::string serial)
        : adb_(std::move(adb)), serial_(std::move(serial)) {}
    ~AdbSession() { stop(); }

    bool alive() const { return alive_; }

    bool start() {
        stop();

        SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE in_r = nullptr, in_w = nullptr, out_r = nullptr, out_w = nullptr;
        if (!CreatePipe(&in_r, &in_w, &sa, 0)) return false;
        if (!CreatePipe(&out_r, &out_w, &sa, 0)) {
            CloseHandle(in_r);
            CloseHandle(in_w);
            return false;
        }
        // 我们只保留 in_w / out_r，另外两个交给子进程并且不留继承
        SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA si = {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        si.hStdInput = in_r;
        si.hStdOutput = out_w;
        si.hStdError = out_w;

        std::string cmd = "\"" + adb_ + "\" -s " + serial_ + " shell";
        std::vector<char> buf(cmd.begin(), cmd.end());
        buf.push_back('\0');

        PROCESS_INFORMATION pi = {};
        BOOL ok = CreateProcessA(nullptr, buf.data(), nullptr, nullptr, TRUE,
                                 CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        CloseHandle(in_r);
        CloseHandle(out_w);
        if (!ok) {
            CloseHandle(in_w);
            CloseHandle(out_r);
            return false;
        }
        CloseHandle(pi.hThread);

        in_ = in_w;
        out_ = out_r;
        proc_ = pi.hProcess;
        alive_ = true;
        return true;
    }

    void stop() {
        alive_ = false;
        if (in_) { CloseHandle(in_); in_ = nullptr; }
        if (out_) { CloseHandle(out_); out_ = nullptr; }
        if (proc_) {
            TerminateProcess(proc_, 0);
            CloseHandle(proc_);
            proc_ = nullptr;
        }
    }

    // 执行一条命令，等哨兵出现；超时或管道断开返回 false
    bool run(const std::string& cmd, std::string& out, int timeout_ms) {
        if (!alive_) return false;
        static const char* kMark = "__CRBOT_DONE_7f3a__";

        const std::string line = cmd + "; echo " + kMark + "\n";
        DWORD written = 0;
        if (!WriteFile(in_, line.data(), static_cast<DWORD>(line.size()), &written,
                       nullptr) ||
            written == 0) {
            stop();
            return false;
        }

        std::string buf;
        const ULONGLONG start = GetTickCount64();
        for (;;) {
            DWORD avail = 0;
            if (!PeekNamedPipe(out_, nullptr, 0, nullptr, &avail, nullptr)) {
                stop();
                return false;
            }
            if (avail > 0) {
                char tmp[4096];
                DWORD got = 0;
                DWORD want = avail < sizeof(tmp) ? avail : static_cast<DWORD>(sizeof(tmp));
                if (!ReadFile(out_, tmp, want, &got, nullptr) || got == 0) {
                    stop();
                    return false;
                }
                buf.append(tmp, got);
                const size_t pos = buf.find(kMark);
                if (pos != std::string::npos) {
                    buf.resize(pos);
                    while (!buf.empty() && (buf.back() == '\n' || buf.back() == '\r')) {
                        buf.pop_back();
                    }
                    out = std::move(buf);
                    return true;
                }
            } else {
                if (GetTickCount64() - start > static_cast<ULONGLONG>(timeout_ms)) {
                    stop();  // 命令卡住：整个会话作废，下次重建
                    return false;
                }
                Sleep(2);
            }
        }
    }

private:
    std::string adb_, serial_;
    HANDLE in_ = nullptr;
    HANDLE out_ = nullptr;
    HANDLE proc_ = nullptr;
    bool alive_ = false;
};

// ========== AdbClient ==========

AdbClient::AdbClient(std::string device_serial)
    : serial_(std::move(device_serial)) {}

AdbClient::~AdbClient() = default;

bool AdbClient::ensure_session() {
    if (session_disabled_) return false;
    {
        std::lock_guard<std::mutex> lk(cancel_.mtx);
        if (cancel_.cancelled) return false;  // 已经被 abort 了，别再起会话
    }
    if (session_ && session_->alive()) return true;

    session_ = std::make_unique<AdbSession>(get_adb_path(), serial_);
    if (!session_->start()) {
        session_.reset();
        if (++session_failures_ >= 3) {
            session_disabled_ = true;
            std::fprintf(stderr, "[ADB] persistent shell unavailable, falling back\n");
        }
        return false;
    }
    session_failures_ = 0;
    return true;
}

bool AdbClient::session_run(const std::string& cmd, std::string& out, int timeout_ms) {
    if (!ensure_session()) return false;
    if (session_->run(cmd, out, timeout_ms)) return true;

    session_.reset();
    if (++session_failures_ >= 3) {
        session_disabled_ = true;
        std::fprintf(stderr, "[ADB] persistent shell disabled, falling back\n");
    }
    return false;
}
std::string AdbClient::connect(const std::string& host_port) {
    return run_process(build_cmd_line(get_adb_path(), "", "connect " + host_port));
}

void AdbClient::start_server() {
    // 先把守护进程拉起来：29 个连接同时去抢着启动 daemon 很容易互相拖死，
    // 而且 daemon 起不来的话这里能立刻报错，而不是每个端口都静默失败。
    std::string out = run_process(build_cmd_line(get_adb_path(), "", "start-server"));
    std::cout << "[ADB] start-server: " << out << std::flush;
}

std::string AdbClient::run_adb(const std::string& args) {
    return run_process(build_cmd_line(get_adb_path(), serial_, args), &cancel_);
}

std::vector<uint8_t> AdbClient::run_adb_binary(const std::string& args) {
    return run_process_binary(build_cmd_line(get_adb_path(), serial_, args), &cancel_);
}

// 从别的线程打断：杀掉当前正在跑的 adb 子进程（screencap 这类）。
// 被打断的线程会从 ReadFile 立刻返回，随后因为 cancelled 之后的命令直接失败。
//
// 注意：这里**不**去 stop 常驻 shell 会话 —— 那个句柄可能正被机器人线程读着，
// 跨线程关句柄是 UB。改成只置 cancelled：会话不再接受新命令，
// 等 Device 析构时自然关闭。代价是卡在 tap 上的线程最多多等它自己的 8 秒超时。
void AdbClient::cancel() {
    std::lock_guard<std::mutex> lk(cancel_.mtx);
    cancel_.cancelled = true;
    if (cancel_.proc) {
        TerminateProcess(static_cast<HANDLE>(cancel_.proc), 1);
    }
}

std::vector<uint8_t> AdbClient::screenshot_raw() {
    // adb exec-out screencap 返回 raw RGBA：
    // 4 字节 width (LE) + 4 字节 height (LE) + 4 字节 format (LE, 1=RGBA_8888)
    // + width*height*4 字节像素数据
    return run_adb_binary("exec-out screencap");
}

std::vector<uint8_t> AdbClient::screenshot_png() {
    return run_adb_binary("exec-out screencap -p");
}

void AdbClient::tap(int x, int y) {
    shell("input tap " + std::to_string(x) + " " + std::to_string(y));
}

void AdbClient::swipe(int x1, int y1, int x2, int y2, int duration_ms) {
    shell("input swipe " + std::to_string(x1) + " " + std::to_string(y1) + " "
          + std::to_string(x2) + " " + std::to_string(y2) + " " + std::to_string(duration_ms));
}

void AdbClient::key_event(int keycode) {
    shell("input keyevent " + std::to_string(keycode));
}

std::string AdbClient::shell(const std::string& cmd) {
    // 常驻会话优先：input tap / swipe / keyevent 这类高频命令不再每次起进程
    std::string out;
    if (session_run(cmd, out, 8000)) return out;
    // 会话不可用（或命令超时被丢弃）时退回一次性进程
    return run_adb("shell " + cmd);
}
