#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// 从**别的线程**打断正在执行的子进程用的令牌：
// 记下当前子进程句柄，cancel_process() 直接杀掉它，
// 卡在 read 里的线程会立刻返回，不用等 60 秒超时。
struct CancelState {
    std::mutex mtx;
    void* proc = nullptr;  // 平台相关：HANDLE / pid，仅 proc_*.cpp 使用
    bool cancelled = false;
};

void cancel_process(CancelState& state);

struct ProcResult {
    std::vector<uint8_t> out;  // stdout + stderr 合并
    int exit_code = 0;
    bool timed_out = false;
};

// 跑一个子进程，把 stdout+stderr 收进返回值。
// 起不来抛 std::runtime_error；超时会杀掉子进程并置 timed_out。
// 注意：不能等管道 EOF —— adb 的守护进程会继承管道的写端，客户端退出后也不会断，
// 所以这里只等【直接子进程】退出，再用一小段静默期确认没有残留输出。
ProcResult run_process(const std::vector<std::string>& argv, int timeout_ms,
                       CancelState* cancel = nullptr);

// 常驻子进程（用于 `adb shell`）：一组管道 + 一个进程。
// 命令写进 stdin，用哨兵标记判断一条命令是否执行完（无 tty 时设备端不会回显）。
class ChildSession {
public:
    ChildSession();
    ~ChildSession();
    ChildSession(const ChildSession&) = delete;
    ChildSession& operator=(const ChildSession&) = delete;

    bool start(const std::vector<std::string>& argv);
    void stop();
    bool alive() const { return alive_; }

    // 写 "cmd; echo marker\n" 并等 marker 出现；超时或管道断开返回 false
    bool run(const std::string& cmd, const std::string& marker, std::string& out,
             int timeout_ms);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool alive_ = false;
};
