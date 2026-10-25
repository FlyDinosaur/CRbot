#pragma once
#include "device_state.h"
#include <windows.h>
#include <vector>

class GuiDashboard {
public:
    GuiDashboard(HINSTANCE hInstance, std::vector<DeviceState*>& states);
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
    void post_request(int idx, int request);

    HINSTANCE hInstance_;
    std::vector<DeviceState*>& states_;
    HWND hwnd_ = nullptr;
    std::vector<HWND> lbl_status_;
    std::vector<HWND> btn_wait_;
    std::vector<HWND> btn_disconnect_;
    std::vector<HWND> btn_stop_;
    std::vector<HWND> btn_retry_;
    int width_ = 620;
};
