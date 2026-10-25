#include "proc.h"

#include <windows.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::string quote_arg(const std::string& a) {
    if (!a.empty() && a.find_first_of(" \t\"") == std::string::npos) return a;
    std::string q = "\"";
    for (char c : a) {
        if (c == '"') q += "\\\"";
        else q += c;
    }
    q += '"';
    return q;
}

std::string build_cmd_line(const std::vector<std::string>& argv) {
    std::string cmd;
    for (size_t i = 0; i < argv.size(); ++i) {
        if (i) cmd += ' ';
        cmd += quote_arg(argv[i]);
    }
    return cmd;
}

}  // namespace

struct ChildSession::Impl {
    HANDLE in = nullptr;
    HANDLE out = nullptr;
    HANDLE proc = nullptr;
};

void cancel_process(CancelState& state) {
    std::lock_guard<std::mutex> lk(state.mtx);
    state.cancelled = true;
    if (state.proc) TerminateProcess(static_cast<HANDLE>(state.proc), 1);
}

ProcResult run_process(const std::vector<std::string>& argv, int timeout_ms,
                       CancelState* cancel) {
    ProcResult res;
    if (argv.empty()) throw std::runtime_error("run_process: empty argv");
    if (cancel) {
        std::lock_guard<std::mutex> lk(cancel->mtx);
        if (cancel->cancelled) throw std::runtime_error("process cancelled");
    }

    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE h_read = nullptr, h_write = nullptr;
    if (!CreatePipe(&h_read, &h_write, &sa, 0)) {
        throw std::runtime_error("CreatePipe failed");
    }
    SetHandleInformation(h_read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = h_write;
    si.hStdError = h_write;

    std::string cmd_line = build_cmd_line(argv);
    std::vector<char> cmd_buf(cmd_line.begin(), cmd_line.end());
    cmd_buf.push_back('\0');

    PROCESS_INFORMATION pi = {};
    const BOOL ok = CreateProcessA(nullptr, cmd_buf.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
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
    bool timed_out = false;

    for (;;) {
        DWORD avail = 0;
        if (PeekNamedPipe(h_read, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            uint8_t buf[8192];
            const DWORD want = avail < sizeof(buf) ? avail : static_cast<DWORD>(sizeof(buf));
            DWORD got = 0;
            if (ReadFile(h_read, buf, want, &got, nullptr) && got > 0) {
                res.out.insert(res.out.end(), buf, buf + got);
                continue;
            }
        }

        if (child_done) {
            // 子进程已经退出，再确认管道里没有残留就可以收工
            DWORD left = 0;
            const bool more =
                PeekNamedPipe(h_read, nullptr, 0, nullptr, &left, nullptr) && left > 0;
            if (!more) break;
            continue;
        }

        const DWORD w = WaitForSingleObject(pi.hProcess, 20);
        if (w == WAIT_OBJECT_0) {
            child_done = true;
        } else if (GetTickCount64() - start > static_cast<ULONGLONG>(timeout_ms)) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 1000);
            timed_out = true;
            child_done = true;
        }
    }

    res.timed_out = timed_out;
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    res.exit_code = static_cast<int>(exit_code);

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

ChildSession::ChildSession() : impl_(std::make_unique<Impl>()) {}
ChildSession::~ChildSession() { stop(); }

bool ChildSession::start(const std::vector<std::string>& argv) {
    stop();
    if (argv.empty()) return false;

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

    std::string cmd = build_cmd_line(argv);
    std::vector<char> buf(cmd.begin(), cmd.end());
    buf.push_back('\0');

    PROCESS_INFORMATION pi = {};
    const BOOL ok = CreateProcessA(nullptr, buf.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(in_r);
    CloseHandle(out_w);
    if (!ok) {
        CloseHandle(in_w);
        CloseHandle(out_r);
        return false;
    }
    CloseHandle(pi.hThread);

    impl_->in = in_w;
    impl_->out = out_r;
    impl_->proc = pi.hProcess;
    alive_ = true;
    return true;
}

void ChildSession::stop() {
    alive_ = false;
    if (!impl_) return;
    if (impl_->in) {
        CloseHandle(impl_->in);
        impl_->in = nullptr;
    }
    if (impl_->out) {
        CloseHandle(impl_->out);
        impl_->out = nullptr;
    }
    if (impl_->proc) {
        TerminateProcess(impl_->proc, 0);
        CloseHandle(impl_->proc);
        impl_->proc = nullptr;
    }
}

bool ChildSession::run(const std::string& cmd, const std::string& marker, std::string& out,
                       int timeout_ms) {
    if (!alive_) return false;

    const std::string line = cmd + "; echo " + marker + "\n";
    DWORD written = 0;
    if (!WriteFile(impl_->in, line.data(), static_cast<DWORD>(line.size()), &written, nullptr) ||
        written == 0) {
        stop();
        return false;
    }

    std::string buf;
    const ULONGLONG start = GetTickCount64();
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(impl_->out, nullptr, 0, nullptr, &avail, nullptr)) {
            stop();
            return false;
        }
        if (avail > 0) {
            char tmp[4096];
            DWORD got = 0;
            const DWORD want = avail < sizeof(tmp) ? avail : static_cast<DWORD>(sizeof(tmp));
            if (!ReadFile(impl_->out, tmp, want, &got, nullptr) || got == 0) {
                stop();
                return false;
            }
            buf.append(tmp, got);
            const size_t pos = buf.find(marker);
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
