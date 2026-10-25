#include <windows.h>
#include "adb_client.h"
#include "device_state.h"
#include "bot_runner.h"
#include "gui_dashboard.h"
#include "log_tee.h"
#include <iostream>

#ifndef CRBOT_VERSION
#define CRBOT_VERSION "dev"
#endif
#include <list>
#include <set>
#include <vector>
#include <future>

namespace {

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && s[i] == ' ') ++i;
    return s.substr(i);
}

// 同一个模拟器可能被好几个端口（5557 / 7555 / 16416 ...）各自连上一次，
// 只看端口会当成多台设备，然后多个线程抢着点同一块屏幕 ——
// 表现就是"菜单刚打开就被关掉"。这里用设备自己的身份去重。
// android_id 每个实例独立、serialno 更通用，两个一起用。
std::string device_identity(const std::string& host) {
    try {
        AdbClient probe(host);
        std::string aid = trim(probe.shell("settings get secure android_id"));
        std::string sn = trim(probe.shell("getprop ro.serialno"));
        if (aid == "null") aid.clear();
        if (!aid.empty() || !sn.empty()) return aid + "|" + sn;
    } catch (...) {
    }
    return host;  // 拿不到就退回端口，至少不会崩
}

}  // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    AllocConsole();
    FILE* dummy;
    freopen_s(&dummy, "CONOUT$", "w", stdout);
    freopen_s(&dummy, "CONOUT$", "w", stderr);
    setvbuf(stdout, nullptr, _IONBF, 0);
    SetConsoleOutputCP(CP_UTF8);  // 中文日志别变乱码

    // 控制台的输出同时写一份到 crbot.log（在 exe 同目录/当前工作目录）
    if (start_log_tee("crbot.log")) {
        std::cout << "[LOG] writing to crbot.log" << std::endl;
    }
    std::cout << "[CRBOT] version " CRBOT_VERSION << std::endl;

    try {
        // 0. 先把 adb 守护进程拉起来（起不来的话这里就会报错，见控制台）
        AdbClient::start_server();

        // 1. 并行扫描模拟器端口
        //    5555~5599：BlueStacks / LDPlayer / MuMu(旧) 常用
        //    16384/16416：MuMu 12 默认；7555：MuMu 6；21503：MEmu；62001/62025：Nox
        std::vector<int> ports;
        for (int port = 5555; port <= 5599; port += 2) ports.push_back(port);
        for (int port : {16384, 16416, 7555, 21503, 62001, 62025}) ports.push_back(port);

        std::vector<std::future<std::pair<std::string, bool>>> futures;
        for (int port : ports) {
            futures.push_back(std::async(std::launch::async, [port]() {
                std::string host = "127.0.0.1:" + std::to_string(port);
                try {
                    std::string result = AdbClient::connect(host);
                    bool ok = result.find("connected") != std::string::npos;
                    return std::make_pair(host, ok);
                } catch (...) {
                    return std::make_pair(host, false);
                }
            }));
        }

        std::list<DeviceState> state_objs;
        std::vector<DeviceState*> state_ptrs;
        std::set<std::string> seen;

        for (auto& f : futures) {
            auto [host, ok] = f.get();
            if (!ok) continue;

            const std::string id = device_identity(host);
            if (!seen.insert(id).second) {
                std::cout << "[DEDUP] " << host << " 与已连接的设备是同一台（serialno="
                          << id << "），跳过" << std::endl;
                continue;
            }
            std::cout << "[CONNECT] " << host << "  (serialno=" << id << ")" << std::endl;
            state_objs.emplace_back(host, "Connected");
        }

        if (state_objs.empty()) {
            MessageBox(nullptr,
                       L"No devices found.\n\n"
                       L"Check that the emulator is running and ADB is enabled,\n"
                       L"then look at the console output for details.",
                       L"CRBot", MB_OK | MB_ICONWARNING);
            return 0;
        }

        for (auto& s : state_objs) state_ptrs.push_back(&s);

        // 2. 启动所有 BotRunner 线程
        std::list<BotRunner> runners;
        std::vector<BotRunner*> runner_ptrs;
        for (auto& s : state_objs) {
            runners.emplace_back(s);
            runner_ptrs.push_back(&runners.back());
        }
        for (auto* r : runner_ptrs) r->start();

        // 3. 启动 GUI。
        //    Retry = 玩家人工介入后，从"连设备"那一步完全重跑（复用 BotRunner::run）；
        //    Stop  = 立即打断，连正在执行的 adb 命令一起杀掉。
        GuiDashboard gui(
            hInstance, state_ptrs,
            [runner_ptrs](size_t i) {
                if (i < runner_ptrs.size()) runner_ptrs[i]->restart();
            },
            [runner_ptrs](size_t i) {
                if (i < runner_ptrs.size()) runner_ptrs[i]->abort();
            });
        gui.run();

        // 4. 退出：停止所有线程
        for (auto& r : runners) r.stop();
        for (auto& r : runners) r.join();

    } catch (const std::exception& e) {
        MessageBoxA(nullptr, e.what(), "Fatal Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    return 0;
}
