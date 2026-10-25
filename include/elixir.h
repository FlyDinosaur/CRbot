#pragma once
#include "image.h"

// 圣水条在 720x1280 下的实测几何（见 docs/圣水识别方案.md）：
//   横向 195..695（10 格 x 50px），纵向 1236..1262，空槽色 (2,52,120)，填充色 (238,84,243)
// 这里用归一化坐标，按实际分辨率换算，因此换机型不用改代码。
struct ElixirBar {
    int x0 = 0, x1 = 0;      // 条的左右边界（含）
    int y0 = 0, y1 = 0;      // 采样带（用条的中间一段，避开圆角和“最多: 10”字样）
    int segments = 10;       // 格数
};

// 对一帧的识别结果
struct ElixirReading {
    bool visible = false;    // 圣水条是否在本帧可见（对局中的强判据）
    int segments = -1;       // 0..10 的整数圣水；-1 表示本帧读不出
    float fill = 0.0f;       // 原始填充比例 0..1（调试用）
    int head_x = -1;         // 填充前沿的 x
    int bar_columns = 0;     // 判定为“像圣水条”的列数，越大越可信
};

ElixirBar default_elixir_bar(int width, int height);

// 纯函数：只依赖像素，方便离线用录像帧做回归测试
ElixirReading read_elixir(const Image& img, const ElixirBar& bar);
