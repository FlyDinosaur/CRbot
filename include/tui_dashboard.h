#pragma once
#include "device_state.h"
#include "log_tee.h"

#include <deque>
#include <mutex>
#include <string>
#include <vector>

// macOS 终端面板：没有 Win32，就用 ANSI 终端画一个等价的设备管理器。
// 上半部分是每台设备的状态，下半部分是实时日志；
// 按键和 Windows 版的按钮一一对应（Stop / Retry / Wait / Disconnect）。
class ConsoleDashboard : public LogSink {
public:
    explicit ConsoleDashboard(std::vector<DeviceState*>& states);
    ~ConsoleDashboard() override;

    // 阻塞到用户按 q 或者 Ctrl-C。stdin 不是终端时退化成行模式（只打印状态变化）。
    void run();

    void on_log(const char* data, size_t size) override;

    // 把当前一帧渲染成纯文本（去掉 ANSI 转义）。给无终端环境下的冒烟测试用。
    std::string frame_text(int cols, int rows);

private:
    std::string build_frame(int cols, int rows);
    struct Row {
        std::string serial;
        std::string status;
        bool awaiting = false;
        bool error = false;
        bool running = false;
    };

    void enter_ui();
    void leave_ui();
    void render();
    void pump_input(int timeout_ms);
    void handle_key(char key);
    void run_line_mode();

    void post_request(int idx, int request);
    void respond(int idx, int response);
    void refresh_rows();

    std::vector<DeviceState*>& states_;
    std::vector<Row> rows_;
    int sel_ = 0;
    int tty_fd_ = -1;  // 指向 /dev/tty：stdout 已经被日志 tee 接走了，不能拿来画画
    bool ui_active_ = false;
    std::string last_frame_;

    std::mutex log_mtx_;
    std::deque<std::string> log_lines_;
    std::string log_partial_;
};
