#include "battler.h"
#include "elixir.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <thread>
#include <utility>

namespace {

using Clock = std::chrono::steady_clock;

long long ms_since(Clock::time_point t) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
}

void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

}  // namespace

Battler::Battler(Device& device, BattleSink sink, const std::atomic<bool>& stop,
                 BattleConfig cfg)
    : device_(device), sink_(std::move(sink)), stop_(stop), cfg_(cfg) {}

void Battler::emit(BattlePhase phase) {
    tele_.phase = phase;
    if (started_at_.time_since_epoch().count() != 0) {
        tele_.elapsed_ms = ms_since(started_at_);
    }
    if (sink_) sink_(tele_);
}

void Battler::tap_jitter(int x, int y) {
    static thread_local std::mt19937 rng(
        static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<int> d(-cfg_.jitter_px, cfg_.jitter_px);
    device_.adb().tap(x + d(rng), y + d(rng));
}

bool Battler::wait_for_start() {
    auto start = Clock::now();
    long long last_logged_s = -1;
    Image last_frame;   // 超时时存盘，用来查"到底卡在哪个画面"

    while (!stop_) {
        emit(BattlePhase::Starting);
        try {
            const auto shot_t0 = Clock::now();
            Image img = device_.screenshot();
            const long long shot_ms = ms_since(shot_t0);
            if (shot_ms > 2000) {
                std::printf("[BATTLE] %s slow screenshot %lldms\n",
                            device_.serial().c_str(), shot_ms);
            }
            last_frame = img;
            ElixirBar bar = default_elixir_bar(img.width(), img.height());
            if (read_elixir(img, bar).visible) {
                started_at_ = Clock::now();
                std::cout << "[BATTLE] " << device_.serial() << " battle detected"
                          << std::endl;
                return true;
            }
        } catch (const std::exception& e) {
            std::cerr << "[BATTLE] screenshot failed: " << e.what() << std::endl;
        }
        if (ms_since(start) > cfg_.start_timeout_s * 1000LL) {
            std::cerr << "[BATTLE] " << device_.serial()
                      << " start timeout after " << ms_since(start) / 1000 << "s" << std::endl;
            if (!last_frame.empty()) {
                std::string name = "crbot_battle_";
                for (char c : device_.serial()) {
                    name += (c == ':' || c == '.') ? '_' : c;
                }
                name += ".png";
                if (last_frame.save_png(name)) {
                    std::cerr << "[BATTLE] saved last frame to " << name << std::endl;
                }
            }
            return false;
        }
        // 每 5 秒报一次，避免"看起来像死了"
        if (ms_since(start) / 5000 != last_logged_s) {
            last_logged_s = ms_since(start) / 5000;
            std::cerr << "[BATTLE] " << device_.serial() << " waiting for battle start ("
                      << ms_since(start) / 1000 << "s)" << std::endl;
        }
        sleep_ms(300);
    }
    return false;
}

// 按轮转顺序挑一张牌：优先可打的，双倍阶段允许退而求其次挑"还灰着"的
int Battler::pick_slot(const CardState* states, int start, bool accept_dim) const {
    int dim = -1;
    for (int i = 0; i < 4; ++i) {
        int idx = (start + i) % 4;
        if (states[idx] == CardState::Ready) return idx;
        if (accept_dim && states[idx] == CardState::Unaffordable && dim < 0) dim = idx;
    }
    return dim;
}

bool Battler::play_card(const CardSlot& slot, int x, int y) {
    // 两次 tap 都是阻塞到 adb 进程退出才算完成；中间的间隔是给"游戏确认选牌"用的。
    tap_jitter(slot.cx(), slot.cy());        // 选中卡牌
    sleep_ms(cfg_.tap_gap_ms);
    tap_jitter(x, y);                        // 落到地上
    // 落点命令返回只代表事件已注入，游戏还要一帧才处理完落点和扣费。
    // 这之后才允许下一次截图，否则拍到的还是扣费前的画面。
    sleep_ms(cfg_.tap_settle_ms);
    return true;
}

bool Battler::run() {
    ElixirBar bar;
    HandGeometry hand;
    bool geom_ready = false;

    int next_slot = 0;
    int last_logged_seg = -1;
    bool last_logged_late = false;
    char last_logged_hand[5] = {0};
    auto last_seen = Clock::now();     // 最后一次看到圣水条
    auto last_play = Clock::now() - std::chrono::seconds(10);
    long long frame_sum_ms = 0;        // 单帧耗时统计
    int frame_count = 0;

    while (!stop_) {
        const auto frame_start = Clock::now();
        Image img;
        try {
            const auto shot_t0 = Clock::now();
            img = device_.screenshot();
            const long long shot_ms = ms_since(shot_t0);
            if (shot_ms > 2000) {
                std::printf("[BATTLE] %s slow screenshot %lldms\n",
                            device_.serial().c_str(), shot_ms);
            }
        } catch (const std::exception& e) {
            std::cerr << "[BATTLE] screenshot failed: " << e.what() << std::endl;
            sleep_ms(300);
            continue;
        }
        if (img.empty()) { sleep_ms(200); continue; }

        if (!geom_ready) {
            bar = default_elixir_bar(img.width(), img.height());
            hand = default_hand(img.width(), img.height());
            geom_ready = true;
        }

        ElixirReading r = read_elixir(img, bar);
        if (!r.visible) {
            // 圣水条被动画遮住是短暂的（实测 <0.3s），长时间看不到就是本局结束
            if (ms_since(last_seen) > cfg_.end_timeout_ms) {
                std::cout << "[BATTLE] " << device_.serial() << " battle over (bar gone "
                          << ms_since(last_seen) << "ms)" << std::endl;
                emit(BattlePhase::Finished);
                return true;
            }
            sleep_ms(120);
            continue;
        }
        last_seen = Clock::now();

        const int raw = r.segments;
        if (raw < 0) { sleep_ms(120); continue; }

        // 读数滤波：**下降立即采信**（出牌扣费是即时的），
        // **上升受时间约束**（双倍圣水约 1 点/1.4s，给 1.5 点余量）。
        // 这样出牌后读到动画里的"假高值"时，不会误以为圣水够而再打一张；
        // 也不能用中值滤波——单帧就要 1.4s，等 3 帧的话滞后近 3 秒。
        int seg = raw;
        const auto now_ts = Clock::now();
        if (last_seg_ >= 0) {
            const long long dt = ms_since(last_seg_at_);
            const int max_rise = static_cast<int>(dt / 1000) + 1;  // ≈1 点/秒 + 1 余量
            if (seg > last_seg_ + max_rise) {
                seg = last_seg_ + max_rise;
            }
        }
        last_seg_ = seg;
        last_seg_at_ = now_ts;

        CardState states[4];
        char hand_txt[8] = {0};
        for (int i = 0; i < 4; ++i) {
            states[i] = read_card(img, hand.slots[i]);
            hand_txt[i] = states[i] == CardState::Ready        ? 'R'
                          : states[i] == CardState::Unaffordable ? 'u'
                          : states[i] == CardState::Empty        ? '-'
                                                                 : '?';
        }

        int ready_count = 0;
        for (int i = 0; i < 4; ++i) {
            if (states[i] == CardState::Ready) ++ready_count;
        }

        tele_.elixir = seg;
        for (int i = 0; i < 4; ++i) tele_.hand[i] = states[i];
        emit(BattlePhase::Running);
        // 只在读数变化时打日志，否则 3Hz 刷屏
        if (seg != last_logged_seg || std::strcmp(hand_txt, last_logged_hand) != 0) {
            std::printf("[BATTLE] elixir %d/10 hand %s\n", seg, hand_txt);
            last_logged_seg = seg;
            std::memcpy(last_logged_hand, hand_txt, sizeof(last_logged_hand));
        }

        // ---- 出牌 ----
        // 117s 后进入双倍圣水：阈值降到 8，并且允许"提前下"（牌还是灰的也下，
        // 游戏在费用即将满足时会接受这一下），否则圣水会一直溢出。
        const bool late_phase =
            started_at_.time_since_epoch().count() != 0 &&
            ms_since(started_at_) >= cfg_.late_after_ms;
        const int threshold = late_phase ? cfg_.late_elixir : cfg_.early_elixir;

        // 自相矛盾的读数：圣水说够、但四张牌全灰。
        // 9 费时不可能有 3 张以上是灰的，说明此刻读到的是扣费动画里的假高值。
        // 用"手牌"作交叉校验，这一帧直接不出牌。
        const bool readings_conflict = seg >= threshold && ready_count == 0;

        if (late_phase != last_logged_late) {
            last_logged_late = late_phase;
            std::printf("[BATTLE] %s phase, play at %d elixir\n",
                        late_phase ? "double-elixir" : "normal", threshold);
        }

        // 下一次"看"游戏必须发生在「tap 的 adb 命令已返回 + 游戏处理完落点」之后，
        // 这两件事分别由 play_card() 里的阻塞调用和 tap_settle_ms 保证。
        if (!readings_conflict && seg >= threshold &&
            ms_since(last_play) >= cfg_.play_interval_ms) {
            int slot = pick_slot(states, next_slot, late_phase && cfg_.late_play_dim);
            if (slot >= 0) {
                play_card(hand.slots[slot], cfg_.deploy_x, cfg_.deploy_y);
                next_slot = (slot + 1) % 4;
                last_play = Clock::now();
                ++tele_.played;
                std::printf("[BATTLE] play slot %d at (%d,%d) with %d elixir%s\n",
                            slot + 1, cfg_.deploy_x, cfg_.deploy_y, seg,
                            states[slot] == CardState::Unaffordable ? " (early/dim)" : "");
            }
        }

        // 单帧耗时：截图 + 识别 + 可能的出牌。这个数才是真正的瓶颈指标
        const long long frame_ms = ms_since(frame_start);
        frame_sum_ms += frame_ms;
        ++frame_count;
        if (frame_count % 20 == 0) {
            std::printf("[BATTLE] frame avg %lldms (%d frames), elixir %d\n",
                        frame_sum_ms / 20, frame_count, seg);
            frame_sum_ms = 0;
        }

        sleep_ms(20);
    }
    return false;
}
