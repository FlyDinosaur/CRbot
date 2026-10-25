#pragma once
#include <string>
#include <mutex>
#include <condition_variable>

struct DeviceState {
    DeviceState() = default;
    DeviceState(std::string serial_, std::string status_)
        : serial(std::move(serial_)), status(std::move(status_)) {}

    std::string serial;
    std::string status;
    bool awaiting_input = false;
    int user_response = 0;   // 0=none, 1=wait, 2=disconnect
    bool running = false;
    bool error = false;

    std::mutex mtx;
    std::condition_variable cv;

    void set_status(std::string s) {
        std::lock_guard<std::mutex> lock(mtx);
        status = std::move(s);
    }

    // 阻塞等待用户选择，返回 true=继续等待, false=断开
    bool wait_for_user() {
        std::unique_lock<std::mutex> lock(mtx);
        awaiting_input = true;
        cv.wait(lock, [this] { return user_response != 0; });
        awaiting_input = false;
        bool ret = (user_response == 1);
        user_response = 0;
        return ret;
    }
};
