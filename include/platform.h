#pragma once
#include <string>
#include <vector>

// 平台相关的小工具：可执行文件位置、adb 查找、路径处理。
// Windows 实现在 platform_win.cpp，macOS/Linux 实现在 platform_posix.cpp。
namespace platform {

// 当前可执行文件所在目录（不带结尾分隔符）；拿不到返回空串。
std::string exe_dir();

// 用户主目录；拿不到返回空串。
std::string home_dir();

bool is_regular_file(const std::string& path);
bool is_executable(const std::string& path);

// 递归创建目录（已存在也算成功）
bool make_dirs(const std::string& path);

// 用 '/' 拼路径（Windows 上也用 '/'，CreateFile 等 API 都接受）
std::string join(const std::string& a, const std::string& b);

// 拆 PATH：POSIX 用 ':'，Windows 用 ';'
std::vector<std::string> split_path_list(const std::string& value);

// 按空白拆命令行，双引号里的空白不拆（不处理转义）
std::vector<std::string> split_args(const std::string& cmd);

// 找一份可用的 adb：CRBOT_ADB > PATH > Android SDK / Homebrew 常见位置。
// Windows 上还会把内嵌的 adb.exe 解出来。找不到返回空串。
std::string resolve_adb();

bool stdin_is_tty();
bool stdout_is_tty();

void sleep_ms(int ms);

}  // namespace platform
