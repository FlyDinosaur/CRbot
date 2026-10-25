#include "hand.h"

#include <algorithm>
#include <cmath>

namespace {

// 卡面区域的“平均饱和度”和“亮度标准差”
struct Stats {
    double sat = 0.0;
    double stddev = 0.0;
    int pixels = 0;
    bool valid() const { return pixels > 0; }
};

Stats region_stats(const Image& img, const CardSlot& s) {
    Stats st;
    double sum_sat = 0.0;
    double sum_l = 0.0, sum_l2 = 0.0;
    for (int y = s.y0; y <= s.y1; ++y) {
        for (int x = s.x0; x <= s.x1; ++x) {
            const uint8_t* p = img.at(x, y);
            if (!p) continue;
            int r = p[0], g = p[1], b = p[2];
            int mx = std::max(r, std::max(g, b));
            int mn = std::min(r, std::min(g, b));
            sum_sat += mx > 0 ? double(mx - mn) / mx : 0.0;
            double l = (r + g + b) / 3.0;
            sum_l += l;
            sum_l2 += l * l;
            ++st.pixels;
        }
    }
    if (!st.valid()) return st;
    st.sat = sum_sat / st.pixels;
    double mean = sum_l / st.pixels;
    double var = std::max(0.0, sum_l2 / st.pixels - mean * mean);
    st.stddev = std::sqrt(var);
    return st;
}

}  // namespace

HandGeometry default_hand(int width, int height) {
    HandGeometry h;
    const int xs[4][2] = {{175, 300}, {305, 430}, {435, 560}, {565, 690}};
    for (int i = 0; i < 4; ++i) {
        h.slots[i].x0 = width * xs[i][0] / 720;
        h.slots[i].x1 = width * xs[i][1] / 720;
        h.slots[i].y0 = height * 1080 / 1280;
        h.slots[i].y1 = height * 1160 / 1280;
    }
    return h;
}

CardState read_card(const Image& img, const CardSlot& slot) {
    Stats st = region_stats(img, slot);
    if (!st.valid()) return CardState::Unknown;
    // 空卡位：几乎纯色（实测 std 12~14，亮/暗卡面都在 45 以上）
    if (st.stddev < 25.0) return CardState::Empty;
    // 压暗：实测 0.03~0.13；可打：0.33~0.74
    if (st.sat < 0.22) return CardState::Unaffordable;
    return CardState::Ready;
}
