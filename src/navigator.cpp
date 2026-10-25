#include "navigator.h"

#include <chrono>
#include <cstdio>
#include <iostream>
#include <thread>

namespace {

void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

long long elapsed_ms(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - t).count();
}

// 阈值：模板是从当前版本截的，正常命中在 0.9 以上；
// 0.72 是为了容忍闪光动画和抗锯齿差异。
constexpr double kMenuScore = 0.72;
constexpr double kOkScore = 0.70;
constexpr double kModeScore = 0.80;

}  // namespace

Navigator::Navigator(Device& device)
    : device_(device),
      rng_(static_cast<unsigned>(
          std::chrono::steady_clock::now().time_since_epoch().count())) {}

void Navigator::tap_jitter(int x, int y) {
    int jx = x + static_cast<int>(jitter_(rng_));
    int jy = y + static_cast<int>(jitter_(rng_));
    device_.adb().tap(jx, jy);
}

Image Navigator::capture() {
    try {
        return device_.screenshot();
    } catch (const std::exception& e) {
        std::cerr << "[NAV] screenshot failed: " << e.what() << std::endl;
        return {};
    }
}

bool Navigator::at_main_menu_in(const Image& img) {
    if (img.empty()) return false;
    return UiButtons::shared().find(img, Btn::Battle, kMenuScore).found;
}

bool Navigator::at_main_menu() { return at_main_menu_in(capture()); }

bool Navigator::navigate_to_1v1() {
    std::cout << "[NAV] " << device_.serial() << " opening mode menu..." << std::endl;

    // 模式菜单的开合按钮图标随当前模式变化（还会随版本改），没法做模板，
    // 只能按固定位置点。菜单项的位置会随滚动变化，所以每次都重新找。
    for (int attempt = 0; attempt < 3; ++attempt) {
        tap_jitter(570, 1000);
        sleep_ms(900);

        for (int scroll = 0; scroll < 4; ++scroll) {
            Image img = capture();
            if (!img.empty()) {
                MatchResult m = UiButtons::shared().find(img, Btn::Mode1v1, kModeScore);
                if (m.found) {
                    std::cout << "[NAV] 经典1对1 at (" << m.cx() << "," << m.cy()
                              << ") score " << m.score << std::endl;
                    tap_jitter(m.cx(), m.cy());
                    sleep_ms(1500);
                    if (at_main_menu()) {
                        std::cout << "[NAV] mode selected" << std::endl;
                        return true;
                    }
                }
            }
            // 没找到 / 选完没回主界面：滑动列表再看
            device_.adb().swipe(360, 800, 360, 620, 500);
            sleep_ms(700);
        }

        tap_jitter(570, 1000);  // 关掉菜单，下一轮重开
        sleep_ms(700);
    }

    // 找不到也别卡住：游戏会记住上次的模式，多半已经是经典1对1
    std::cerr << "[NAV] 1v1 item not found; keeping current mode" << std::endl;
    return at_main_menu();
}

bool Navigator::start_match() {
    // 结算后的每日奖励、开箱、赛季拼图都是**动画**，可能要连着过好几层，
    // 所以这里最多等 120 秒，期间不断点空白处推进——不能等 20 秒就放弃。
    std::cerr << "[NAV] waiting for battle button..." << std::endl;

    auto start = std::chrono::steady_clock::now();
    int polls = 0;
    MatchResult btn;
    bool found = false;
    while (true) {
        Image img = capture();
        if (!img.empty()) {
            btn = UiButtons::shared().find(img, Btn::Battle, kMenuScore);
            if (btn.found) { found = true; break; }
        }
        if (elapsed_ms(start) > 120000) break;
        // 每 4 轮（约 2 秒）点一下空白处，把奖励动画推过去
        if (++polls % 4 == 0) tap_jitter(365, 500);
        sleep_ms(300);
    }
    if (!found) {
        std::cerr << "[NAV] battle button not found after " << elapsed_ms(start) / 1000 << "s"
                  << std::endl;
        save_debug_frame();
        return false;
    }

    // 点"对战"这一下有时会被游戏吞掉（加载中/转场），点完要验证：5 秒内按钮应消失
    for (int attempt = 1; attempt <= 3; ++attempt) {
        std::cerr << "[NAV] battle button at (" << btn.cx() << "," << btn.cy() << ") score "
                  << btn.score << ", tapping (attempt " << attempt << "/3)" << std::endl;
        tap_jitter(btn.cx(), btn.cy());

        // 验证：按钮消失 = 已进入匹配
        auto t1 = std::chrono::steady_clock::now();
        bool left = false;
        while (elapsed_ms(t1) < 5000) {
            sleep_ms(400);
            Image img = capture();
            if (img.empty()) continue;
            if (!UiButtons::shared().find(img, Btn::Battle, kMenuScore).found) {
                left = true;
                break;
            }
        }
        if (left) {
            std::cerr << "[NAV] matchmaking started" << std::endl;
            return true;
        }
        std::cerr << "[NAV] tap did not take effect, retry " << attempt << "/3" << std::endl;
        sleep_ms(600);
    }
    return false;
}

bool Navigator::finish_match() {
    if (at_main_menu()) {
        std::cerr << "[NAV] already at main menu" << std::endl;
        return true;
    }

    std::cerr << "[NAV] leaving battle result..." << std::endl;
    sleep_ms(1200);

    // 结算 → 每日首胜奖励 → 宝箱 → 主界面，中间可能好几层。
    // 奖励/拼图这类界面是**纯动画、没有可匹配的特征**，只能点空白处推进；
    // 所以这里只允许两个动作：
    //   1) 命中模板的「确定」→ 点它
    //   2) 否则点空白处 (365,500) 推进动画
    // 绝不能点 (360,1000) 之类的区域——那是底部「对战」按钮，会直接开下一局。
    for (int i = 0; i < 24; ++i) {
        Image img = capture();
        if (img.empty()) { sleep_ms(400); continue; }

        MatchResult battle = UiButtons::shared().find(img, Btn::Battle, kMenuScore);
        if (battle.found) {
            std::cerr << "[NAV] back to main menu" << std::endl;
            return true;
        }

        MatchResult ok = UiButtons::shared().find(img, Btn::Ok, kOkScore);
        std::fprintf(stderr, "[NAV] result iter %d: battle=%.2f ok=%.2f%s\n", i + 1,
                     battle.score, ok.score, ok.found ? " -> tap 确定" : " -> tap blank");

        if (ok.found) {
            tap_jitter(ok.cx(), ok.cy());
        } else {
            tap_jitter(365, 500);  // 空白处，只推进动画
        }

        // 前几轮就存一张现场图，方便看清是哪一层界面
        if (i == 2) save_debug_frame();

        sleep_ms(700);
    }

    std::cerr << "[NAV] could not confirm main menu" << std::endl;
    save_debug_frame();
    return false;
}

void Navigator::save_debug_frame() {
    Image img = capture();
    if (img.empty()) return;
    const char* path = "crbot_debug.png";
    if (img.save_png(path)) {
        std::cerr << "[NAV] saved current frame to " << path << std::endl;
    }
}
