"""Build a validation montage: bar crops with the computed elixir value.

帧目录：命令行第 1 个参数 > 环境变量 CRBOT_FRAMES > ./frames
输入 CSV 与输出 PNG 都在本脚本所在目录。
"""
import csv
import os
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
FRAMES = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("CRBOT_FRAMES", "frames")
CSV = os.path.join(HERE, "frames_elixir.csv")
OUT = os.path.join(HERE, "elixir_validation.png")

rows = {int(r["frame"]): r for r in csv.DictReader(open(CSV, encoding="utf-8"))}
picks = [32, 500, 900, 1200, 1500, 1800, 2100, 2400, 2600, 2750, 2830, 2870]

crops = []
for n in picks:
    r = rows[n]
    img = Image.open(os.path.join(FRAMES, f"{n:06d}.png")).convert("RGB").crop((130, 1205, 710, 1275))
    crops.append((n, r, img))

W, H = 580, 70
canvas = Image.new("RGB", (W + 150, H * len(crops)), (20, 20, 20))
d = ImageDraw.Draw(canvas)
for i, (n, r, img) in enumerate(crops):
    y = i * H
    canvas.paste(img, (150, y))
    d.text((6, y + 8), f"#{n}", fill=(200, 255, 200))
    d.text((6, y + 26), f"e={r['elixir']}", fill=(255, 255, 120))
    d.text((6, y + 44), f"in={r['in_battle']} y={r['y0']}-{r['y1']}", fill=(180, 200, 255))
canvas = canvas.resize((canvas.width * 2, canvas.height * 2), Image.NEAREST)
canvas.save(OUT)
print(OUT, canvas.size)
