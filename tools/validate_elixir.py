"""Validate elixir curve: slope should be 1/2.8 per second; plays show drops."""
import csv
import numpy as np

CSV = r"D:\file\CRBot\tools\frames_elixir.csv"
rows = list(csv.DictReader(open(CSV, encoding="utf-8")))
FPS = 60.0
X0, SEG = 195.0, 50.0

ser = []
for r in rows:
    e = int(r["end"])
    val = 0.0 if e <= 0 else min(10.0, (e + 1 - X0) / SEG)
    ser.append(val)
ser = np.array(ser)

# smooth: median filter to kill 1-frame glitches
med = ser.copy()
for i in range(1, len(ser) - 1):
    med[i] = np.median(ser[max(0, i - 2):i + 3])

# slopes over 0.5s windows while not at cap
d = []
for i in range(0, len(med) - 30, 5):
    a, b = med[i], med[i + 30]
    if a < 9 and b > a:
        d.append((b - a) / (30 / FPS))
d = np.array(d)
print(f"rise samples={len(d)} mean={d.mean():.3f} median={np.median(d):.3f} "
      f"p10={np.percentile(d,10):.3f} p90={np.percentile(d,90):.3f} (theory 0.357)")

drops = []
for i in range(1, len(med)):
    if med[i - 1] - med[i] > 0.7:
        drops.append((i, round(med[i - 1], 2), round(med[i], 2)))
print("drops (frame, before, after):")
print(drops[:40])
print("count:", len(drops))

# distribution of fractional parts (should be uniform-ish if continuous)
frac = np.array([v for v in med if 0.2 < v < 9.8])
print("fractional part histogram:", np.histogram(frac % 1.0, bins=5)[0])


def ascii_curve(ser, width=110, height=12):
    idx = np.linspace(0, len(ser) - 1, width).astype(int)
    vals = ser[idx]
    grid = [[" "] * width for _ in range(height)]
    for c, v in enumerate(vals):
        r = height - 1 - int(round(v / 10 * (height - 1)))
        grid[r][c] = "*"
    return "\n".join("".join(row) for row in grid)


print(ascii_curve(med))
