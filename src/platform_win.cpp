#include "platform.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <io.h>
#include <string>
#include <vector>

namespace platform {

std::string exe_dir() {
    char buf[MAX_PATH] = {0};
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    std::string p(buf, n);
    auto pos = p.find_last_of("\\/");
    return pos == std::string::npos ? std::string{} : p.substr(0, pos);
}

std::string home_dir() {
    if (const char* p = std::getenv("USERPROFILE"); p && *p) return p;
    return {};
}

bool is_regular_file(const std::string& path) {
    const DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool is_executable(const std::string& path) {
    // Windows 不看执行位，存在就算
    return is_regular_file(path);
}

bool make_dirs(const std::string& path) {
    if (path.empty()) return false;
    CreateDirectoryA(path.c_str(), nullptr);
    return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::string join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    const bool a_slash = a.back() == '/' || a.back() == '\\';
    const bool b_slash = b.front() == '/' || b.front() == '\\';
    if (a_slash && b_slash) return a + b.substr(1);
    if (a_slash || b_slash) return a + b;
    return a + "\\" + b;
}

std::vector<std::string> split_path_list(const std::string& value) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : value) {
        if (c == ';') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::vector<std::string> split_args(const std::string& cmd) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (char c : cmd) {
        if (c == '"') {
            quoted = !quoted;
            continue;
        }
        if (!quoted && (c == ' ' || c == '\t' || c == '\n' || c == '\r')) {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
            continue;
        }
        cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool stdin_is_tty() { return _isatty(_fileno(stdin)) != 0; }
bool stdout_is_tty() { return _isatty(_fileno(stdout)) != 0; }

void sleep_ms(int ms) {
    if (ms > 0) Sleep(static_cast<DWORD>(ms));
}

}  // namespace platform
