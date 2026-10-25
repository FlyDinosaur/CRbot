#pragma once
#include "device_state.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

class AdbClient;

class BotRunner {
public:
    BotRunner(DeviceState& state);
    ~BotRunner();

    void start();
    void stop();
    void join();

    // 完全重来：停掉当前线程，清空状态，从"连设备"那一步重新跑。
    // 给玩家人工介入（手动关弹窗、重新登录等）之后用。
    void restart();

    // 立即打断：不等当前 adb 命令超时，直接把子进程杀掉。
    void abort();

private:
    void run();

    DeviceState& state_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> restarting_{false};
    std::unique_ptr<std::thread> thread_;

    // 正在使用的 adb 客户端（run() 里建好 Device 后登记，退出前注销）
    std::mutex adb_mtx_;
    AdbClient* adb_ = nullptr;
};
