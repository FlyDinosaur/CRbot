"""C++ 实现的 Python 镜像（逐行对应 src/elixir.cpp、src/hand.cpp）。

用来在改 C++ 之前先确认判据和阈值是合理的，也用来和 elixir_probe 的输出对拍。
输出格式与 elixir_probe 完全一致：file,visible,segments,fill,hand
"""
import os
import sys
import numpy as np
from PIL import Image

FRAMES = r"D:\file\Users\nya\Documents\MuMu共享文件夹\VideoRecords\frames"

# ---- 与 default_elixir_bar() 一致（整数运算，避免浮点取整误差）----
W, H = 720, 1280
X0 = W * 195 // 720      # 195
X1 = W * 695 // 720      # 695
YC = H * 1248 // 1280    # 1248
HALF = max(4, H * 10 // 1280)
Y0, Y1 = YC - HALF, YC + HALF
SEGMENTS = 10
VISIBLE_PCT = 80         # ← 由 95 调整为 80（对局帧最低 91.8%，非对局帧最高 20.4%）

# ---- 与 default_hand() 一致 ----
SLOT_X = [(175, 300), (305, 430), (435, 560), (565, 690)]
ART_Y = (1080, 1160)


def read_elixir(a):
    sub = a[Y0:Y1 + 1, X0:X1 + 1].astype(np.int16)
    r, g, b = sub[..., 0], sub[..., 1], sub[..., 2]
    mx = np.maximum(np.maximum(r, g), b)
    mn = np.minimum(np.minimum(r, g), b)
    fill = (r >= 100) & (b >= 100) & (g <= (np.minimum(r, b) * 85) // 100)
    empty = (mx >= 80) & (mx <= 190) & (mn <= 70)

    rows, cols = sub.shape[0], sub.shape[1]
    fill_rows = fill.sum(axis=0)
    empty_rows = empty.sum(axis=0)

    filled = fill_rows * 2 > rows
    xs = np.where(filled)[0]
    head = int(xs[-1]) if xs.size else -1
    bar_like = int((((fill_rows + empty_rows) * 10) >= rows * 9).sum())

    if bar_like * 100 < cols * VISIBLE_PCT:
        return False, -1, 0.0
    if head < 0:
        return True, 0, 0.0
    seg_w = cols / SEGMENTS
    units = (head + 1) / seg_w
    return True, int(round(min(max(units, 0.0), 10.0))), min(units / SEGMENTS, 1.0)


def read_card(a, x0, x1):
    sub = a[ART_Y[0]:ART_Y[1] + 1, x0:x1 + 1].astype(np.float32)
    mx = sub.max(axis=2)
    mn = sub.min(axis=2)
    sat = float(np.mean((mx - mn) / np.maximum(mx, 1.0)))
    lum = sub.mean(axis=2)
    std = float(np.std(lum))
    if std < 25.0:
        return "-"
    if sat < 0.22:
        return "u"
    return "R"


def main():
    out = open(sys.argv[1] if len(sys.argv) > 1 else "tools/mirror.csv", "w", encoding="utf-8")
    visible = 0
    hist = [0] * 11
    total = 0
    for name in sorted(os.listdir(FRAMES)):
        a = np.asarray(Image.open(os.path.join(FRAMES, name)).convert("RGB"))
        vis, seg, fill = read_elixir(a)
        hand = "".join(read_card(a, *s) for s in SLOT_X)
        out.write(f"{name},{1 if vis else 0},{seg},{fill:.3f},{hand}\n")
        total += 1
        if vis:
            visible += 1
            hist[max(0, min(10, seg))] += 1
    out.close()
    print(f"frames={total} visible={visible}")
    print("segment histogram:", {i: hist[i] for i in range(11)})


if __name__ == "__main__":
    main()
