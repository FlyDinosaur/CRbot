#include "cr_game.h"
#include "ui_buttons.h"
#include <iostream>
#include <thread>
#include <chrono>

CRGame::CRGame(Device& device) : device_(device) {}

bool CRGame::is_installed() {
    std::string result = device_.adb().shell("pm list packages " + std::string(PACKAGE));
    // 安装了的话输出 "package:com.tencent.tmgp.supercell.clashroyale"
    return result.find(PACKAGE) != std::string::npos;
}

bool CRGame::is_running() {
    // pidof 返回 PID 字符串，未运行则抛异常（exit != 0）
    try {
        std::string result = device_.adb().shell("pidof " + std::string(PACKAGE));
        return !result.empty() && result.find_first_not_of(" \t\n\r") != std::string::npos;
    } catch (...) {
        return false;
    }
}

void CRGame::launch() {
    std::cout << "[LAUNCH] " << device_.serial()
              << " starting " << PACKAGE << std::endl;
    device_.adb().shell("am start -n " + std::string(PACKAGE) + "/" + std::string(ACTIVITY));
}

void CRGame::ensure_running() {
    if (!is_installed()) {
        throw std::runtime_error(
            std::string(PACKAGE) + " is not installed on " + device_.serial());
    }

    // 不管游戏是否在运行，都 am start（已在运行则拉到前台）
    launch();
    std::this_thread::sleep_for(std::chrono::seconds(8));
    std::cout << "[GAME] " << device_.serial() << " launched" << std::endl;
}

bool CRGame::wait_for_main_menu(DeviceState* state, int timeout_seconds) {
    auto start = std::chrono::steady_clock::now();

    while (true) {
        if (state) state->set_status("Checking for main menu...");

        // 主界面判据 = 底部黄色"对战"按钮能匹配上（像素，不依赖 OCR）
        try {
            Image img = device_.screenshot();
            if (UiButtons::shared().find(img, Btn::Battle, 0.72).found) {
                if (state) state->set_status("Main menu detected");
                return true;
            }
        } catch (const std::exception& e) {
            std::cerr << "[GAME] screenshot failed: " << e.what() << std::endl;
        }

        auto elapsed = std::chrono::steady_clock::now() - start;
        auto sec = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();

        if (sec >= timeout_seconds) {
            if (state) {
                state->set_status("Login timeout - awaiting user input");
                if (!state->wait_for_user()) return false;
            } else {
                std::string choice;
                std::cin >> choice;
                if (choice == "d" || choice == "D") return false;
            }
            start = std::chrono::steady_clock::now();
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
