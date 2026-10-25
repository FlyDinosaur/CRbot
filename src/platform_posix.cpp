#include "platform.h"

#include <mach-o/dyld.h>
#include <sys/stat.h>
#include <unistd.h>

#include <climits>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace platform {

std::string exe_dir() {
    std::vector<char> buf(4096, '\0');
    uint32_t size = static_cast<uint32_t>(buf.size());
    if (_NSGetExecutablePath(buf.data(), &size) != 0) {
        buf.resize(size + 1);
        if (_NSGetExecutablePath(buf.data(), &size) != 0) return {};
    }

    // _NSGetExecutablePath 可能带 ../ 和符号链接，realpath 收拾干净
    char resolved[PATH_MAX];
    if (::realpath(buf.data(), resolved) != nullptr) {
        return fs::path(resolved).parent_path().string();
    }
    return fs::path(buf.data()).parent_path().string();
}

std::string home_dir() {
    if (const char* h = std::getenv("HOME"); h && *h) return h;
    return {};
}

bool is_regular_file(const std::string& path) {
    struct stat st {};
    return !path.empty() && ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool is_executable(const std::string& path) {
    struct stat st {};
    return !path.empty() && ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
           ::access(path.c_str(), X_OK) == 0;
}

bool make_dirs(const std::string& path) {
    if (path.empty()) return false;
    std::error_code ec;
    fs::create_directories(path, ec);
    return fs::is_directory(path, ec);
}

std::string join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    const bool a_slash = a.back() == '/' || a.back() == '\\';
    const bool b_slash = b.front() == '/' || b.front() == '\\';
    if (a_slash && b_slash) return a + b.substr(1);
    if (a_slash || b_slash) return a + b;
    return a + "/" + b;
}

std::vector<std::string> split_path_list(const std::string& value) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : value) {
        if (c == ':') {
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

std::string resolve_adb() {
    if (const char* env = std::getenv("CRBOT_ADB"); env && *env) {
        if (is_executable(env)) return env;
    }

    // 1. PATH
    if (const char* path = std::getenv("PATH"); path && *path) {
        for (const std::string& dir : split_path_list(path)) {
            const std::string cand = join(dir, "adb");
            if (is_executable(cand)) return cand;
        }
    }

    // 2. Android SDK / Homebrew / MacPorts 的常见位置
    std::vector<std::string> cands;
    for (const char* var : {"ANDROID_HOME", "ANDROID_SDK_ROOT"}) {
        if (const char* sdk = std::getenv(var); sdk && *sdk) {
            cands.push_back(join(join(sdk, "platform-tools"), "adb"));
        }
    }
    const std::string home = home_dir();
    if (!home.empty()) {
        cands.push_back(join(join(join(home, "Library/Android/sdk"), "platform-tools"), "adb"));
        cands.push_back(join(join(home, "Android/Sdk"), "platform-tools/adb"));
    }
    cands.push_back("/opt/homebrew/bin/adb");   // Apple Silicon Homebrew
    cands.push_back("/usr/local/bin/adb");      // Intel Homebrew / 官方 zip 手动解压
    cands.push_back("/opt/local/bin/adb");      // MacPorts

    // 3. 跟着可执行文件找（打包时把 adb 放旁边）
    const std::string base = exe_dir();
    if (!base.empty()) {
        cands.push_back(join(base, "adb"));
        cands.push_back(join(base, "platform-tools/adb"));
        cands.push_back(join(base, "../platform-tools/adb"));
    }

    for (const std::string& cand : cands) {
        if (is_executable(cand)) return cand;
    }
    return {};
}

bool stdin_is_tty() { return ::isatty(STDIN_FILENO) != 0; }
bool stdout_is_tty() { return ::isatty(STDOUT_FILENO) != 0; }

void sleep_ms(int ms) {
    if (ms <= 0) return;
    ::usleep(static_cast<useconds_t>(ms) * 1000);
}

}  // namespace platform
