#pragma once
#include "device.h"
#include "device_state.h"
#include <atomic>
#include <string>

class CRGame {
public:
    // 国服皇室战争
    static constexpr const char* PACKAGE  = "com.tencent.tmgp.supercell.clashroyale";
    static constexpr const char* ACTIVITY = "com.supercell.titan.tencent.GameAppTencent";

    explicit CRGame(Device& device);

    bool is_installed();   // 游戏是否已安装
    bool is_running();     // 游戏是否在前台/后台运行
    void launch();         // 启动游戏
    // 未运行则启动，未安装则抛异常。stop 置位时提前返回（退出不用等满 8 秒）
    void ensure_running(const std::atomic<bool>* stop = nullptr);

    // 等待进入主界面。state 非空时通过 GUI 交互，为空时使用 stdin。
    // stop 非空时，置位后立刻返回 false（退出程序 / Stop 不用等 60 秒超时）。
    // 返回 false 表示用户选择断开此设备
    bool wait_for_main_menu(DeviceState* state = nullptr, int timeout_seconds = 60,
                            const std::atomic<bool>* stop = nullptr);

private:
    Device& device_;
};
