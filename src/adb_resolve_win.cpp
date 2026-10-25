#include "platform.h"
#include "adb_resource.h"

#include <windows.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Windows 版 adb 查找：优先把内嵌的 adb.exe 解到本地，必要时退回磁盘上现成的。
// 单独一个文件，这样 elixir_probe / ui_probe 只用到 platform::exe_dir 时
// 不必把 34MB 的 adb_data 一起编进来。
namespace {

bool file_of_size(const std::string& path, unsigned int want) {
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fad)) return false;
    return fad.nFileSizeHigh == 0 && fad.nFileSizeLow == want;
}

// adb 启动守护进程时会先在临时目录里开 adb.log，开不了就 "failed to start daemon"。
// 有的机器 %TEMP% 对当前用户不可写（策略 / 安全软件 / ACL 损坏），
// 所以这里先找一个确实能写的地方，再把子进程的 TEMP/TMP 指过去。
std::string prepare_tmp_dir() {
    std::vector<std::string> cands;
    if (const char* env = std::getenv("CRBOT_TMP"); env && *env) {
        cands.push_back(env);
    }
    const std::string base = platform::exe_dir();
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

// 找一份可用的 adb。优先级：
//   0. %CRBOT_ADB% 环境变量（手动指定，调试用）
//   1. 已经解出来的副本（大小对得上就复用）—— adb 守护进程会锁住自己的镜像文件，
//      重复 truncate 会失败
//   2. 往 %LOCALAPPDATA%\CRBot\ 或 %TEMP% 落一份
//   3. 退回 exe 旁边 / 工程根目录下的 tools\adb.exe
std::string extract_adb_impl() {
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
    const std::string base = platform::exe_dir();
    for (const std::string& rel : {"\\tools\\adb.exe", "\\..\\tools\\adb.exe"}) {
        std::string cand = base + rel;
        if (!base.empty() && GetFileAttributesA(cand.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return cand;
        }
    }

    throw std::runtime_error("Cannot extract adb (tried:" + tried +
                             "), and no tools\\adb.exe next to the executable");
}

}  // namespace

namespace platform {

std::string resolve_adb() {
    if (const char* env = std::getenv("CRBOT_ADB"); env && *env) {
        if (is_regular_file(env)) return env;
    }

    const std::string tmp = prepare_tmp_dir();
    if (!tmp.empty()) std::cout << "[ADB] tmp=" << tmp << std::endl;
    return extract_adb_impl();
}

}  // namespace platform
