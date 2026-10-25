"""Validate the card-availability rule (dim = unaffordable) against elixir readings.

帧目录：命令行第 1 个参数 > 环境变量 CRBOT_FRAMES > ./frames
"""
import csv
import os
import sys

from PIL import Image
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
FRAMES = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("CRBOT_FRAMES", "frames")
SLOTS = [(175, 300), (305, 430), (435, 560), (565, 690)]
ART_Y = (1080, 1160)

rows = {int(r["frame"]): r for r in csv.DictReader(
    open(os.path.join(HERE, "frames_elixir.csv"), encoding="utf-8"))}

files = sorted(os.listdir(FRAMES))[::7]
viol_low = []
viol_high = []
empty_seen = 0
for name in files:
    n = int(os.path.splitext(name)[0])
    a = np.asarray(Image.open(os.path.join(FRAMES, name)).convert("RGB")).astype(np.float32)
    e = float(rows[n]["elixir"])
    for i, (x0, x1) in enumerate(SLOTS):
        sub = a[ART_Y[0]:ART_Y[1], x0:x1]
        mx = sub.max(axis=2)
        mn = sub.min(axis=2)
        sat = float(((mx - mn) / np.maximum(mx, 1)).mean())
        std = float(sub.mean(axis=2).std())
        empty = std < 25
        playable = (not empty) and sat >= 0.22
        if empty:
            empty_seen += 1
            continue
        if e <= 0.5 and playable:
            viol_low.append((n, i + 1, round(sat, 2)))
        if e >= 9.5 and not playable:
            viol_high.append((n, i + 1, round(sat, 2)))

print("sampled frames:", len(files), "empty slot samples:", empty_seen)
print("violations at elixir 0 (card shown playable):", len(viol_low), viol_low[:10])
print("violations at elixir >=9.5 (card shown dim):", len(viol_high), viol_high[:10])
