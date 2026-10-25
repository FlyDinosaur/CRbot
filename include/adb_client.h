#pragma once
#include "proc.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// 用于从别的线程打断正在执行的 adb 命令（定义见 proc.h）：
// 记下当前子进程句柄，cancel() 时直接杀掉，
// 这样卡在 screencap 里的线程会立刻返回，不用等 60 秒超时。
using AdbCancelState = CancelState;

class AdbSession;  // 常驻 adb shell 会话（定义在 adb_client.cpp）

class AdbClient {
public:
    explicit AdbClient(std::string device_serial);
    ~AdbClient();  // 在 cpp 里定义：AdbSession 在这里是不完整类型

    // 禁止拷贝/移动：内含 cancel_ 的 mutex，移动本来就不可用（原来写 default 也是被删除的）
    AdbClient(const AdbClient&) = delete;
    AdbClient& operator=(const AdbClient&) = delete;
    AdbClient(AdbClient&&) = delete;
    AdbClient& operator=(AdbClient&&) = delete;

    // ---- 静态：连接 ----
    static std::string connect(const std::string& host_port);  // 例如 "127.0.0.1:5557"
    static void start_server();                                // 先拉起 adb 守护进程

    // 已经连上、状态是 device 的序列号（USB 真机 / emulator-5554 / 网络设备）
    static std::vector<std::string> list_devices();

    // ---- 设备操作 ----
    std::vector<uint8_t> screenshot_raw();   // adb exec-out screencap -> raw RGBA + 12B header
    std::vector<uint8_t> screenshot_png();   // adb exec-out screencap -p -> PNG
    void tap(int x, int y);
    void swipe(int x1, int y1, int x2, int y2, int duration_ms = 300);
    void key_event(int keycode);
    std::string shell(const std::string& cmd);

    // 打断：杀掉正在执行的 adb 命令，并让后续命令直接失败。
    // 可以从任意线程调用。
    void cancel();

    const std::string& serial() const { return serial_; }

private:
    static std::string get_adb_path();           // 懒加载，整个进程只找一次 adb
    std::string run_adb(const std::string& args);
    std::vector<uint8_t> run_adb_binary(const std::string& args);

    // 常驻 shell：成功返回 true，命令输出写进 out
    bool ensure_session();
    bool session_run(const std::string& cmd, std::string& out, int timeout_ms);

    std::string serial_;
    std::unique_ptr<AdbSession> session_;
    bool session_disabled_ = false;  // 反复起不来就别再试了
    int session_failures_ = 0;
    AdbCancelState cancel_;
};
