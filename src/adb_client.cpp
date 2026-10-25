#include "adb_client.h"
#include "platform.h"
#include "proc.h"

#include <cstdio>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {

constexpr int kAdbTimeoutMs = 60000;
constexpr int kDevicesTimeoutMs = 20000;

std::vector<std::string> adb_argv(const std::string& adb, const std::string& serial,
                                  const std::string& args) {
    std::vector<std::string> v;
    v.push_back(adb);
    if (!serial.empty()) {
        v.push_back("-s");
        v.push_back(serial);
    }
    for (const std::string& token : platform::split_args(args)) v.push_back(token);
    return v;
}

std::string text_of(const std::vector<uint8_t>& bytes) {
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

ProcResult run_adb_process(const std::vector<std::string>& argv, int timeout_ms,
                           CancelState* cancel = nullptr) {
    return run_process(argv, timeout_ms, cancel);
}

}  // namespace

// 找一份可用的 adb：
//   Windows —— 内嵌的 adb.exe 解到本地（见 platform_win.cpp）；
//   macOS   —— CRBOT_ADB > PATH > Android SDK / Homebrew 常见位置。
std::string AdbClient::get_adb_path() {
    static std::string path = [] {
        const std::string p = platform::resolve_adb();
        if (p.empty()) {
            throw std::runtime_error(
                "adb not found.\n"
                "  macOS:  brew install --cask android-platform-tools\n"
                "  or put adb in PATH / set CRBOT_ADB=/path/to/adb\n"
                "  (Windows 上会自动使用内嵌的 adb.exe)");
        }
        std::cout << "[ADB] using " << p << std::endl;
        return p;
    }();
    return path;
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

    bool alive() const { return session_.alive(); }

    bool start() {
        session_.stop();
        return session_.start({adb_, "-s", serial_, "shell"});
    }

    void stop() { session_.stop(); }

    bool run(const std::string& cmd, std::string& out, int timeout_ms) {
        static const char* kMark = "__CRBOT_DONE_7f3a__";
        return session_.run(cmd, kMark, out, timeout_ms);
    }

private:
    std::string adb_, serial_;
    ChildSession session_;
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
    const ProcResult r =
        run_adb_process(adb_argv(get_adb_path(), "", "connect " + host_port), kAdbTimeoutMs);
    const std::string out = text_of(r.out);
    if (r.timed_out) throw std::runtime_error("adb connect timed out: " + host_port);
    if (r.exit_code != 0) {
        throw std::runtime_error("adb connect failed (exit " + std::to_string(r.exit_code) +
                                 "): " + host_port + "\n" + out);
    }
    return out;
}

void AdbClient::start_server() {
    // 先把守护进程拉起来：29 个连接同时去抢着启动 daemon 很容易互相拖死，
    // 而且 daemon 起不来的话这里能立刻报错，而不是每个端口都静默失败。
    const ProcResult r =
        run_adb_process(adb_argv(get_adb_path(), "", "start-server"), kAdbTimeoutMs);
    const std::string out = text_of(r.out);
    if (r.timed_out) throw std::runtime_error("adb start-server timed out\n" + out);
    if (r.exit_code != 0) {
        throw std::runtime_error("adb start-server failed (exit " +
                                 std::to_string(r.exit_code) + ")\n" + out);
    }
    std::cout << "[ADB] start-server: " << (out.empty() ? "ok\n" : out) << std::flush;
}

std::vector<std::string> AdbClient::list_devices() {
    const ProcResult r =
        run_adb_process(adb_argv(get_adb_path(), "", "devices"), kDevicesTimeoutMs);
    const std::string out = text_of(r.out);
    if (r.timed_out) throw std::runtime_error("adb devices timed out\n" + out);
    if (r.exit_code != 0) {
        throw std::runtime_error("adb devices failed (exit " + std::to_string(r.exit_code) +
                                 ")\n" + out);
    }

    std::vector<std::string> serials;
    std::istringstream in(out);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '*') continue;
        if (line.rfind("List of devices", 0) == 0) continue;
        if (line.rfind("adb server", 0) == 0) continue;

        std::string serial, state;
        const auto tab = line.find('\t');
        if (tab != std::string::npos) {
            serial = line.substr(0, tab);
            state = line.substr(tab + 1);
        } else {
            std::istringstream ls(line);
            if (!(ls >> serial >> state)) continue;
        }
        const auto sp = state.find_first_of(" \t");
        if (sp != std::string::npos) state.resize(sp);
        if (state == "device" && !serial.empty()) serials.push_back(serial);
    }
    return serials;
}

std::string AdbClient::run_adb(const std::string& args) {
    const ProcResult r =
        run_adb_process(adb_argv(get_adb_path(), serial_, args), kAdbTimeoutMs, &cancel_);
    const std::string out = text_of(r.out);
    if (r.timed_out) {
        throw std::runtime_error("adb command timed out: " + args + "\n" + out);
    }
    if (r.exit_code != 0) {
        throw std::runtime_error("adb command failed (exit " + std::to_string(r.exit_code) +
                                 "): " + args + "\n" + out);
    }
    return out;
}

std::vector<uint8_t> AdbClient::run_adb_binary(const std::string& args) {
    const ProcResult r =
        run_adb_process(adb_argv(get_adb_path(), serial_, args), kAdbTimeoutMs, &cancel_);
    if (r.timed_out) throw std::runtime_error("adb command timed out: " + args);
    if (r.exit_code != 0) {
        throw std::runtime_error("adb command failed (exit " + std::to_string(r.exit_code) +
                                 "): " + args + "\n" + text_of(r.out));
    }
    return r.out;
}

// 从别的线程打断：杀掉当前正在跑的 adb 子进程（screencap 这类）。
// 被打断的线程会从阻塞的读取立刻返回，随后因为 cancelled 之后的命令直接失败。
//
// 注意：这里**不**去 stop 常驻 shell 会话 —— 那个管道可能正被机器人线程读着，
// 跨线程关闭是 UB。改成只置 cancelled：会话不再接受新命令，
// 等 Device 析构时自然关闭。代价是卡在 tap 上的线程最多多等它自己的 8 秒超时。
void AdbClient::cancel() { cancel_process(cancel_); }

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
