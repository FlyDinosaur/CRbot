#include "log_tee.h"

#include <windows.h>

#include <fcntl.h>
#include <io.h>

namespace {

HANDLE g_console = INVALID_HANDLE_VALUE;
HANDLE g_file = INVALID_HANDLE_VALUE;
HANDLE g_pipe_read = INVALID_HANDLE_VALUE;

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

}  // namespace

bool start_log_tee(const char* path) {
    g_console = GetStdHandle(STD_OUTPUT_HANDLE);

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
    if (CreateThread(nullptr, 0, pump, nullptr, 0, nullptr) == nullptr) {
        CloseHandle(read_end);
        CloseHandle(write_end);
        CloseHandle(g_file);
        g_pipe_read = INVALID_HANDLE_VALUE;
        g_file = INVALID_HANDLE_VALUE;
        return false;
    }

    // 把 CRT 的 stdout / stderr 指向管道写端。
    // fd 和 write_end 都故意不关：管道写端一关，后台线程就会收到 EOF 退出。
    int fd = _open_osfhandle(reinterpret_cast<intptr_t>(write_end), _O_TEXT);
    if (fd == -1) return false;
    _dup2(fd, 1);
    _dup2(fd, 2);
    return true;
}
