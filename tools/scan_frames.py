"""Scan all match frames and measure the elixir bar (fixed band, robust classifier)."""
import csv
import os
from PIL import Image
import numpy as np

FRAMES = r"D:\file\Users\nya\Documents\MuMu共享文件夹\VideoRecords\frames"
OUT = r"D:\file\CRBot\tools\frames_elixir.csv"

X0, X1 = 195, 695
SEG = 50.0
Y0, Y1 = 1238, 1258


def analyse(path):
    a = np.asarray(Image.open(path).convert("RGB")).astype(np.int16)
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    empty = (b >= 90) & (b <= 210) & (r <= 90) & (g <= 115)
    fill = (r >= 100) & (b >= 100) & (np.abs(r - b) <= 70) & (g <= 215) & ~empty
    band_e = empty[Y0:Y1 + 1, X0:X1].mean(axis=0)
    band_f = fill[Y0:Y1 + 1, X0:X1].mean(axis=0)
    # bar present when nearly every column is either empty-blue or fill
    if (band_e + band_f).mean() < 0.9:
        return dict(in_battle=0, end=-1, elixir=0.0)
    filled = band_f > 0.5
    xs = np.where(filled)[0]
    end = X0 + int(xs[-1]) if xs.size else 0
    elixir = 0.0 if end == 0 else max(0.0, min(10.0, (end + 1 - X0) / SEG))
    return dict(in_battle=1, end=end, elixir=round(elixir, 2))


files = sorted(os.listdir(FRAMES))
rows = []
for i, name in enumerate(files):
    r = analyse(os.path.join(FRAMES, name))
    r["frame"] = int(os.path.splitext(name)[0])
    rows.append(r)
    if i % 500 == 0:
        print(f"{i}/{len(files)}", flush=True)

with open(OUT, "w", newline="", encoding="utf-8") as fh:
    w = csv.DictWriter(fh, fieldnames=["frame", "in_battle", "end", "elixir"])
    w.writeheader()
    w.writerows(rows)

bad = [r["frame"] for r in rows if not r["in_battle"]]
print("in_battle:", len(rows) - len(bad), "/", len(rows))
if bad:
    print("missing frames:", bad[:30], "count", len(bad))
