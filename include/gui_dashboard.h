#pragma once
#include "device_state.h"
#include <windows.h>
#include <functional>
#include <vector>

class GuiDashboard {
public:
    // on_retry：点了"重试"（完全重跑）；on_abort：点了"中止"（立即打断）
    GuiDashboard(HINSTANCE hInstance, std::vector<DeviceState*>& states,
                 std::function<void(size_t)> on_retry = {},
                 std::function<void(size_t)> on_abort = {});
    ~GuiDashboard();
    void run();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void create_controls(HWND hwnd);
    void refresh();
    void on_wait(int idx);
    void on_disconnect(int idx);
    void on_retry(int idx);
    void on_abort(int idx);

    HINSTANCE hInstance_;
    std::vector<DeviceState*>& states_;
    std::function<void(size_t)> on_retry_;
    std::function<void(size_t)> on_abort_;
    HWND hwnd_ = nullptr;
    std::vector<HWND> lbl_status_;
    std::vector<HWND> btn_wait_;
    std::vector<HWND> btn_disconnect_;
    std::vector<HWND> btn_stop_;
    std::vector<HWND> btn_retry_;
    int width_ = 620;
};
