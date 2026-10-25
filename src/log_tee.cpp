#include "log_tee.h"

#if defined(_WIN32)

#include <windows.h>

#include <fcntl.h>
#include <io.h>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>

namespace {

HANDLE g_console = INVALID_HANDLE_VALUE;
HANDLE g_file = INVALID_HANDLE_VALUE;
HANDLE g_pipe_read = INVALID_HANDLE_VALUE;
HANDLE g_pump_thread = INVALID_HANDLE_VALUE;
int g_pipe_write_fd = -1;  // 管道写端的 CRT fd（fd 1/2 是它的副本）

DWORD WINAPI pump(LPVOID) {
    char buf[4096];
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(g_pipe_read, buf, sizeof(buf), &got, nullptr) || got == 0) break;

        DWORD written = 0;
        if (g_console != INVALID_HANDLE_VALUE) {
            WriteConsoleA(g_console, buf, got, &written, nullptr);
        }
        if (g_file != INVALID_HANDLE_VALUE) {
            WriteFile(g_file, buf, got, &written, nullptr);
            FlushFileBuffers(g_file);  // 崩了也不丢日志
        }
    }
    return 0;
}

// 进程退出前把管道里剩下的日志读干净再走。
// 不这么做的话，`main` 里最后几行（尤其是"没有找到设备"这种）会随进程一起丢掉。
void shutdown_tee() {
    std::cout.flush();
    std::cerr.flush();
    std::fflush(nullptr);

    // CRT 的每个 fd 各持有一份句柄副本，三个都要关，管道写端才真正断开
    if (g_pipe_write_fd >= 0) {
        _close(g_pipe_write_fd);
        g_pipe_write_fd = -1;
    }
    _close(1);
    _close(2);

    if (g_pump_thread != INVALID_HANDLE_VALUE) {
        WaitForSingleObject(g_pump_thread, 2000);  // pump 读到 EOF 后会把剩余日志写完
        CloseHandle(g_pump_thread);
        g_pump_thread = INVALID_HANDLE_VALUE;
    }
}

}  // namespace

bool start_log_tee(const char* path) {
    // Release 版没有 AllocConsole，这里拿到的可能是空句柄：
    // 那就只写文件，pump 里会跳过控制台写入。
    g_console = GetStdHandle(STD_OUTPUT_HANDLE);
    if (g_console == nullptr || g_console == INVALID_HANDLE_VALUE) {
        g_console = INVALID_HANDLE_VALUE;
    }

    g_file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_file == INVALID_HANDLE_VALUE) return false;

    // UTF-8 BOM，记事本/VS Code 都能正确识别中文
    DWORD written = 0;
    const char bom[] = "\xEF\xBB\xBF";
    WriteFile(g_file, bom, 3, &written, nullptr);

    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE read_end = nullptr, write_end = nullptr;
    if (!CreatePipe(&read_end, &write_end, &sa, 0)) {
        CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
        return false;
    }

    g_pipe_read = read_end;

    // 把 CRT 的 stdout / stderr 指向管道写端。
    int fd = _open_osfhandle(reinterpret_cast<intptr_t>(write_end), _O_TEXT);
    if (fd == -1) {
        CloseHandle(read_end);
        CloseHandle(write_end);
        CloseHandle(g_file);
        g_pipe_read = INVALID_HANDLE_VALUE;
        g_file = INVALID_HANDLE_VALUE;
        return false;
    }
    g_pipe_write_fd = fd;

    g_pump_thread = CreateThread(nullptr, 0, pump, nullptr, 0, nullptr);
    if (g_pump_thread == nullptr) {
        _close(fd);
        g_pipe_write_fd = -1;
        CloseHandle(read_end);
        CloseHandle(g_file);
        g_pipe_read = INVALID_HANDLE_VALUE;
        g_file = INVALID_HANDLE_VALUE;
        return false;
    }

    _dup2(fd, 1);
    _dup2(fd, 2);
    std::atexit(shutdown_tee);  // 退出前把管道里剩下的日志读干净
    return true;
}

// Windows 的界面不是终端面板，不需要订阅日志。
void set_log_sink(LogSink*) {}

#else  // POSIX（macOS）

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>

namespace {

int g_file = -1;
int g_pipe_read = -1;
int g_pipe_write = -1;
int g_console_fd = -1;  // dup 出来的原始 stdout：dup2 之后 fd 1 已经是管道了
std::thread g_thread;
std::mutex g_sink_mtx;
LogSink* g_sink = nullptr;  // 受 g_sink_mtx 保护

void emit(const char* data, size_t size) {
    LogSink* sink = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_sink_mtx);
        sink = g_sink;
    }
    if (sink) {
        sink->on_log(data, size);
        return;
    }
    if (g_console_fd < 0) return;
    size_t off = 0;
    while (off < size) {
        const ssize_t n = ::write(g_console_fd, data + off, size - off);
        if (n > 0) {
            off += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        break;
    }
}

void pump() {
    char buf[4096];
    for (;;) {
        const ssize_t n = ::read(g_pipe_read, buf, sizeof buf);
        if (n > 0) {
            if (g_file >= 0) {
                size_t off = 0;
                while (off < static_cast<size_t>(n)) {
                    const ssize_t w = ::write(g_file, buf + off, static_cast<size_t>(n) - off);
                    if (w > 0) {
                        off += static_cast<size_t>(w);
                        continue;
                    }
                    if (w < 0 && errno == EINTR) continue;
                    break;
                }
                ::fsync(g_file);  // 崩了也不丢日志
            }
            emit(buf, static_cast<size_t>(n));
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        break;
    }
}

// 进程退出前把管道里剩下的日志读干净再走。
// 不这么做的话，`main` 里最后几行（尤其是"没有找到设备"这种）会随进程一起丢掉。
void shutdown_tee() {
    std::cout.flush();
    std::cerr.flush();
    std::fflush(nullptr);
    if (g_pipe_write >= 0) {
        ::close(g_pipe_write);
        g_pipe_write = -1;
    }
    ::close(STDOUT_FILENO);
    ::close(STDERR_FILENO);
    if (g_thread.joinable()) g_thread.join();
}

}  // namespace

bool start_log_tee(const char* path) {
    // 子进程死掉后写管道会拿到 SIGPIPE，直接杀进程；日志场景一律忽略
    ::signal(SIGPIPE, SIG_IGN);

    int fds[2] = {-1, -1};
    if (::pipe(fds) != 0) return false;
    const int file = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file < 0) {
        ::close(fds[0]);
        ::close(fds[1]);
        return false;
    }

    // UTF-8 BOM
    const char bom[] = "\xEF\xBB\xBF";
    if (::write(file, bom, 3) != 3) {
        ::close(file);
        ::close(fds[0]);
        ::close(fds[1]);
        return false;
    }

    g_file = file;
    g_pipe_read = fds[0];
    g_console_fd = ::dup(STDOUT_FILENO);

    // stdout 换成管道后默认是全缓冲，日志会看起来"卡住"；改成行缓冲。
    std::setvbuf(stdout, nullptr, _IOLBF, 0);

    g_pipe_write = fds[1];
    g_thread = std::thread(pump);
    std::atexit(shutdown_tee);
    if (::dup2(fds[1], STDOUT_FILENO) < 0 || ::dup2(fds[1], STDERR_FILENO) < 0) {
        return false;
    }
    return true;
}

void set_log_sink(LogSink* sink) {
    std::lock_guard<std::mutex> lk(g_sink_mtx);
    g_sink = sink;
}

#endif
