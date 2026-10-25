#pragma once
#include "device.h"
#include "hand.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <string>

// 对局内的出牌策略参数。改这里即可调打法，不用动逻辑。
struct BattleConfig {
    int early_elixir = 9;       // 前 117s：攒到 9 费再下
    int late_elixir = 8;        // 117s 后（双倍圣水）：8 费就下，否则会溢出
    long long late_after_ms = 117000;  // 什么时候切到双倍圣水节奏
    bool late_play_dim = true;  // 双倍阶段允许"提前下"：牌还是灰的也下
    int deploy_x = 360;         // 下牌点（720x1280 实测：河边中央）
    int deploy_y = 695;
    // 出牌→落点两次 adb 命令本身是阻塞的（等到 adb 进程退出），所以下一张牌
    // 只要等一个兜底间隔即可；真正防连下靠"出牌后重置中值滤波"。
    int play_interval_ms = 600;
    int tap_gap_ms = 120;       // 选牌 → 落点之间，给游戏确认选牌的时间
    int tap_settle_ms = 180;    // 落点之后，给游戏处理落点/扣费的时间
    int end_timeout_ms = 4000;  // 圣水条消失多久算本局结束
    int start_timeout_s = 60;   // 匹配 + 加载最长等待
    int jitter_px = 8;          // 点击随机偏移，避免每次点同一点
};

enum class BattlePhase { Starting, Running, Finished };

// 对局内循环对外播报的内容。Battler 不认识 GUI / DeviceState，
// 只把这一小段数据丢给上层，显示成什么样由上层决定。
struct BattleTelemetry {
    BattlePhase phase = BattlePhase::Starting;
    int elixir = -1;           // 0..10，-1 = 本帧没读出来
    CardState hand[4] = {CardState::Unknown, CardState::Unknown,
                         CardState::Unknown, CardState::Unknown};
    int played = 0;            // 本局已出牌数
    long long elapsed_ms = 0;  // 本局已进行时间
};

using BattleSink = std::function<void(const BattleTelemetry&)>;

// 一局对战的主循环：读圣水 → 读手牌 → 出牌 → 判结束。
// 只负责"对局内"：进场判据 wait_for_start()，出场判据 run() 返回。
class Battler {
public:
    Battler(Device& device, BattleSink sink, const std::atomic<bool>& stop,
            BattleConfig cfg = {});

    // 等圣水条出现（= 匹配成功，进入对局）
    bool wait_for_start();

    // 打完整局，返回 true 表示正常结束（圣水条消失）
    bool run();

private:
    void emit(BattlePhase phase);
    bool play_card(const CardSlot& slot, int x, int y);
    int pick_slot(const CardState* states, int start, bool accept_dim) const;
    void tap_jitter(int x, int y);

    Device& device_;
    BattleSink sink_;
    const std::atomic<bool>& stop_;
    BattleConfig cfg_;
    BattleTelemetry tele_;
    std::chrono::steady_clock::time_point started_at_{};
    // 圣水读数滤波用：上一帧采信的值 + 时刻
    int last_seg_ = -1;
    std::chrono::steady_clock::time_point last_seg_at_{};
};
