#pragma once
#include "image.h"

// 手牌（底部 4 张卡）状态。游戏本身会把“圣水不够”的卡压暗，
// 所以不需要知道卡的费用，也能判断这张牌现在能不能打。
enum class CardState {
    Empty,          // 卡位刚打出，正在补牌（深蓝空位）
    Unaffordable,   // 压暗 = 圣水不够
    Ready,          // 亮着 = 现在可以打
    Unknown,
};

struct CardSlot {
    int x0 = 0, x1 = 0;
    int y0 = 0, y1 = 0;
    int cx() const { return (x0 + x1) / 2; }
    int cy() const { return (y0 + y1) / 2; }
};

struct HandGeometry {
    CardSlot slots[4];
};

// 720x1280 实测：4 个卡位 x = [175,300] [305,430] [435,560] [565,690]，卡面 y 1080..1160
HandGeometry default_hand(int width, int height);

CardState read_card(const Image& img, const CardSlot& slot);
