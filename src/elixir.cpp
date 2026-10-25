#include "elixir.h"

#include <algorithm>
#include <cmath>

namespace {

inline int max3(int r, int g, int b) { return std::max(r, std::max(g, b)); }

// 圣水填充色（品红/亮紫）：R、B 都高，G 明显偏低。
// 这样判不会把白色动画闪屏（R=G=B）算成填充，也不受某些机型 R/B 通道顺序影响。
//   实测填充 (238,84,243)  脉冲时 (255,157,255)  空槽 (2,52,120)  前沿辉光 (54,87,160)
inline bool is_fill(const uint8_t* p) {
    int r = p[0], g = p[1], b = p[2];
    int lo = std::min(r, b);
    return r >= 100 && b >= 100 && g <= lo * 85 / 100;
}

// 空槽是那种很暗的深蓝：最亮通道不高，最暗通道接近 0
inline bool is_empty_slot(const uint8_t* p) {
    int mx = max3(p[0], p[1], p[2]);
    int mn = std::min(p[0], std::min(p[1], p[2]));
    return mx >= 80 && mx <= 190 && mn <= 70;
}

}  // namespace

ElixirBar default_elixir_bar(int width, int height) {
    if (width <= 0 || height <= 0) return {};
    // 720x1280 -> x 195..695, y 1238..1258。全用整数运算，避免浮点取整偏差一格。
    ElixirBar bar;
    bar.x0 = width * 195 / 720;
    bar.x1 = width * 695 / 720;
    const int yc = height * 1248 / 1280;
    const int half = std::max(4, height * 10 / 1280);
    bar.y0 = yc - half;
    bar.y1 = yc + half;
    bar.segments = 10;
    return bar;
}

ElixirReading read_elixir(const Image& img, const ElixirBar& bar) {
    ElixirReading out;
    if (img.empty()) return out;
    if (bar.x1 <= bar.x0 || bar.y1 <= bar.y0) return out;
    if (bar.x1 > img.width() || bar.y1 >= img.height()) return out;

    const int rows = bar.y1 - bar.y0 + 1;
    const int cols = bar.x1 - bar.x0 + 1;
    if (rows < 3 || cols < bar.segments * 4) return out;

    int head_x = -1;      // 最后一个“填充”列
    int bar_like = 0;     // 明显是圣水条（填充或空槽）的列

    for (int x = bar.x0; x <= bar.x1; ++x) {
        int fill_rows = 0, empty_rows = 0;
        for (int y = bar.y0; y <= bar.y1; ++y) {
            const uint8_t* p = img.at(x, y);
            if (!p) continue;
            if (is_fill(p)) ++fill_rows;
            else if (is_empty_slot(p)) ++empty_rows;
        }
        if (fill_rows * 2 > rows) head_x = x;
        if ((fill_rows + empty_rows) * 10 >= rows * 9) ++bar_like;
    }

    out.head_x = head_x;
    out.bar_columns = bar_like;

    // 圣水条整条都是“填充色或空槽色”。实测对局帧占 91.8%~97.4% 的列
    // （满圣水时左边两位数的“10”会多占几列，所以最低是 91.8%），
    // 而菜单/登录页只有约 20%，取 80% 两边都有足够余量。
    if (bar_like * 100 < cols * 80) {
        out.visible = false;
        return out;
    }
    out.visible = true;

    const double seg_w = static_cast<double>(cols) / bar.segments;
    if (head_x < 0) {
        out.segments = 0;
        out.fill = 0.0f;
        return out;
    }

    double units = (head_x + 1 - bar.x0) / seg_w;
    // 动画脉冲会让前沿冲过头 1~4px，四舍五入回最近的整格
    out.fill = static_cast<float>(std::clamp(units / bar.segments, 0.0, 1.0));
    out.segments = static_cast<int>(std::lround(std::clamp(units, 0.0, 10.0)));
    return out;
}
