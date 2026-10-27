#!/usr/bin/env python3
"""Generate a synthetic test scene (BMP) for the film previews.

Sky gradient with a sun, layered hills, a row of colour patches (including a
skin tone) and a grey ramp, so tone curves, colour shifts and grain all show.

    python3 make_scene.py scene.bmp [width height]
"""

import math
import struct
import sys

W, H = 1024, 768
if len(sys.argv) > 3:
    W, H = int(sys.argv[2]), int(sys.argv[3])

PATCHES = [
    (115, 82, 68),   # dark skin
    (194, 150, 130), # light skin
    (98, 122, 157),  # blue sky
    (87, 108, 67),   # foliage
    (214, 126, 44),  # orange
    (80, 91, 166),   # purplish blue
    (193, 90, 99),   # moderate red
    (157, 188, 64),  # yellow green
    (224, 163, 46),  # orange yellow
    (56, 61, 150),   # blue
    (70, 148, 73),   # green
    (175, 54, 60),   # red
]


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def pixel(x, y):
    u, v = x / W, y / H

    # grey ramp along the bottom
    if v > 0.9:
        g = int(255 * u)
        return (g, g, g)

    # colour patches
    if 0.74 < v < 0.88:
        n = len(PATCHES)
        i = min(int(u * n), n - 1)
        cx = (u * n) - i
        if 0.08 < cx < 0.92:
            return PATCHES[i]
        return (30, 30, 32)

    # hills
    h1 = 0.52 + 0.05 * math.sin(u * 5.0 + 1.0) + 0.02 * math.sin(u * 13.0)
    h2 = 0.60 + 0.04 * math.sin(u * 3.0 + 2.5)
    if v > h2:
        shade = 0.55 + 0.45 * (1 - (v - h2) / (0.74 - h2 + 1e-6))
        return lerp((24, 52, 20), (110, 150, 52), shade)
    if v > h1:
        shade = (v - h1) / (h2 - h1 + 1e-6)
        return lerp((74, 104, 118), (46, 82, 60), shade)

    # sky with sun
    sky = lerp((62, 104, 178), (238, 186, 140), v / 0.6)
    d = math.hypot(u - 0.72, (v - 0.30) * H / W)
    if d < 0.045:
        return (255, 244, 214)
    glow = max(0.0, 1 - d / 0.25) ** 2
    return lerp(sky, (255, 226, 170), glow * 0.8)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "scene.bmp"
    stride = (W * 3 + 3) & ~3
    with open(out, "wb") as f:
        f.write(b"BM" + struct.pack("<IHHI", 54 + stride * H, 0, 0, 54))
        f.write(struct.pack("<IiiHHIIiiII", 40, W, H, 1, 24, 0, stride * H, 2835, 2835, 0, 0))
        pad = b"\0" * (stride - W * 3)
        for y in range(H - 1, -1, -1):
            row = bytearray()
            for x in range(W):
                r, g, b = pixel(x, y)
                row += bytes((b, g, r))
            f.write(row + pad)


if __name__ == "__main__":
    main()
