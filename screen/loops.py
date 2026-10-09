#!/usr/bin/env python3
"""Render full-screen loops for the AL80 GIF page (96x160). Each one loops seamlessly.

    loops.py                 writes screen/loops/<name>.gif and a gallery page
"""
import math
import pathlib
import random

from PIL import Image, ImageDraw, ImageFilter

W, H = 96, 160
OUT = pathlib.Path(__file__).parent / "loops"
N = 40  # frames per loop


def rain():
    """Digital rain: green glyph columns falling at different speeds."""
    random.seed(3)
    cw, ch = 6, 8
    cols, rows = W // cw, H // ch
    glyphs = []
    for _ in range(24):  # tiny random 4x6 glyphs
        g = Image.new("L", (4, 6), 0)
        px = g.load()
        for y in range(6):
            for x in range(4):
                px[x, y] = 255 if random.random() < 0.45 else 0
        glyphs.append(g)
    lanes = [{"speed": random.choice((1, 1, 2)), "off": random.randrange(rows * 2), "len": random.randint(6, 14),
              "g": [random.randrange(24) for _ in range(rows)]} for _ in range(cols)]
    frames = []
    for f in range(N):
        img = Image.new("RGB", (W, H), (0, 6, 2))
        for c, lane in enumerate(lanes):
            period = rows * 2
            head = (lane["off"] + f * lane["speed"] * period // N) % period
            for r in range(rows):
                d = (head - r) % period
                if d < lane["len"]:
                    k = 1 - d / lane["len"]
                    color = (190, 255, 200) if d == 0 else (0, int(60 + 195 * k), int(20 + 50 * k))
                    glyph = glyphs[(lane["g"][r] + (f // 5 if d < 2 else 0)) % 24]
                    img.paste(color, (c * cw + 1, r * ch + 1), glyph)
        frames.append(img)
    return frames, 12


def sunset():
    """Synthwave: striped sun over a perspective grid that scrolls toward the viewer."""
    frames = []
    horizon = 92
    for f in range(N):
        img = Image.new("RGB", (W, H), (0, 0, 0))
        px = img.load()
        for y in range(horizon):  # sky
            t = y / horizon
            for x in range(W):
                px[x, y] = (int(20 + 90 * t), int(4 + 20 * t), int(60 + 70 * t))
        cx, cy, r = 48, 62, 30
        for y in range(cy - r, min(cy + r, horizon)):  # sun with widening gaps toward the bottom
            band = (y - cy + r) / (2 * r)
            if band > 0.5 and ((y + f // 4) % 6) < (band - 0.45) * 7:
                continue
            half = int(math.sqrt(max(0, r * r - (y - cy) ** 2)))
            for x in range(cx - half, cx + half):
                px[x, y] = (255, int(214 - 150 * band), int(64 + 80 * band))
        draw = ImageDraw.Draw(img)
        draw.rectangle([0, horizon, W, H], fill=(14, 2, 34))
        for i in range(-10, 11):  # grid lines running to the vanishing point
            draw.line([48 + i * 3, horizon, 48 + i * 30, H], fill=(255, 46, 190), width=1)
        phase = f / N
        for k in range(9):       # cross lines, spaced by perspective
            z = (k + phase) / 9
            y = horizon + int((H - horizon) * z * z)
            draw.line([0, y, W, y], fill=(255, 46, 190) if z > 0.25 else (150, 20, 120), width=1)
        draw.line([0, horizon, W, horizon], fill=(255, 150, 230), width=1)
        frames.append(img)
    return frames, 12


def warp():
    """Hyperspace: stars streaking out from the centre."""
    random.seed(5)
    stars = [{"a": random.uniform(0, 6.283), "ph": random.random(), "tint": random.choice(((255, 255, 255), (170, 210, 255), (255, 225, 190)))} for _ in range(110)]
    frames = []
    for f in range(N):
        img = Image.new("RGB", (W, H), (2, 3, 12))
        draw = ImageDraw.Draw(img)
        for s in stars:
            t = (s["ph"] + f / N) % 1.0
            r0, r1 = (t ** 2.2) * 110, (min(1.0, t + 0.05) ** 2.2) * 110
            x0, y0 = 48 + math.cos(s["a"]) * r0, 80 + math.sin(s["a"]) * r0 * 1.25
            x1, y1 = 48 + math.cos(s["a"]) * r1, 80 + math.sin(s["a"]) * r1 * 1.25
            k = min(1.0, 0.2 + t * 1.2)
            draw.line([x0, y0, x1, y1], fill=tuple(int(c * k) for c in s["tint"]), width=1 if t < 0.6 else 2)
        frames.append(img)
    return frames, 15


def lava():
    """Lava lamp: slow blobs merging and parting."""
    blobs = [(30, 40, 22, 1, 0.0), (66, 110, 26, 1, 1.7), (48, 80, 18, 2, 3.1), (24, 126, 16, 1, 4.4), (70, 34, 15, 2, 5.2)]
    frames = []
    for f in range(N):
        t = f / N * 2 * math.pi
        field = Image.new("L", (W, H), 0)
        px = field.load()
        pos = [(x + math.sin(t * k + p) * 12, y + math.cos(t * k + p * 1.3) * 26, r) for x, y, r, k, p in blobs]
        for y in range(H):
            for x in range(W):
                v = sum(r * r / ((x - bx) ** 2 + (y - by) ** 2 + 1) for bx, by, r in pos)
                px[x, y] = 255 if v > 2.4 else (int(255 * max(0.0, (v - 1.5) / 0.9)) if v > 1.5 else 0)
        img = Image.new("RGB", (W, H), (26, 4, 38))
        img.paste((255, 90, 30), mask=field.point(lambda v: 110 if 0 < v < 255 else 0).filter(ImageFilter.GaussianBlur(1.5)))
        img.paste((255, 170, 40), mask=field.point(lambda v: 255 if v == 255 else 0))
        hot = field.point(lambda v: 255 if v == 255 else 0).filter(ImageFilter.MinFilter(7)).filter(ImageFilter.GaussianBlur(2))
        img.paste((255, 232, 150), mask=hot)
        frames.append(img)
    return frames, 10


LOOPS = {"digital-rain": rain, "synthwave-sunset": sunset, "hyperspace": warp, "lava-lamp": lava}


def main():
    OUT.mkdir(exist_ok=True)
    cards = []
    for name, make in LOOPS.items():
        frames, fps = make()
        frames[0].save(OUT / f"{name}.gif", save_all=True, append_images=frames[1:], duration=round(1000 / fps), loop=0, disposal=1)
        cards.append(f'<figure><img src="{name}.gif" alt="{name}"><figcaption>{name}</figcaption></figure>')
        print(f"{name}: {len(frames)} frames at {fps} fps")
    page = ("<!doctype html><meta charset=utf-8><title>AL80 loops</title><style>"
            "body{background:#111;color:#ddd;font:15px system-ui;margin:32px}h1{font-weight:600;font-size:20px}"
            "main{display:flex;gap:32px;flex-wrap:wrap}figure{margin:0;text-align:center}"
            "img{width:288px;height:480px;image-rendering:pixelated;border-radius:6px;border:1px solid #333;display:block;margin-bottom:10px}"
            "</style><h1>Full-screen loops for the GIF page, shown at 3x (96x160 on the keyboard)</h1><main>" + "".join(cards) + "</main>")
    (OUT / "index.html").write_text(page)


if __name__ == "__main__":
    main()
