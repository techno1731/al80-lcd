#!/usr/bin/env python3
"""Home page strip animations that are not eyes (96x64, at most 42 frames, seamless loops).

    strips.py                writes screen/strips/<name>.gif and a gallery page with every option
"""
import math
import pathlib
import random

from PIL import Image, ImageDraw, ImageFilter

W, H, SS, N = 96, 64, 4, 40
HERE = pathlib.Path(__file__).parent
OUT = HERE / "strips"
TAU = 2 * math.pi


def big(color=(0, 0, 0)):
    return Image.new("RGB", (W * SS, H * SS), color)


def small(img):
    return img.resize((W, H), Image.LANCZOS)


def _lighter(a, b):
    from PIL import ImageChops
    return ImageChops.lighter(a, b)


def equalizer():
    """A spectrum of bars, each with its own rhythm and a peak marker that falls slowly."""
    bars = 12
    random.seed(31)
    voices = [(random.choice((1, 2, 3)), random.uniform(0, TAU), random.choice((2, 3, 4)), random.uniform(0, TAU)) for _ in range(bars)]
    frames = []
    for f in range(N):
        t = f / N
        img = big((6, 4, 14))
        d = ImageDraw.Draw(img)
        for i, (k1, p1, k2, p2) in enumerate(voices):
            level = 0.5 + 0.3 * math.sin(t * TAU * k1 + p1) + 0.2 * math.sin(t * TAU * k2 + p2)
            segs = max(1, round(level * 11))
            x = 5 + i * 7.3
            for sgm in range(segs):
                y = 58 - sgm * 5
                color = (40, 220, 255) if sgm < 6 else ((190, 110, 255) if sgm < 9 else (255, 70, 150))
                d.rectangle([x * SS, (y - 3.4) * SS, (x + 5.6) * SS, y * SS], fill=color)
            peak = 58 - min(11, segs + 1) * 5
            d.rectangle([x * SS, (peak - 1) * SS, (x + 5.6) * SS, peak * SS], fill=(255, 255, 255))
        frames.append(small(_lighter(img.filter(ImageFilter.GaussianBlur(1.6 * SS)).point(lambda v: int(v * 0.6)), img)))
    return frames


def heartbeat():
    """A monitor trace sweeping across, with the pulse spike and a fading tail."""
    def wave(u):  # one beat, u in 0..1
        if 0.30 < u < 0.34: return -0.12
        if 0.34 <= u < 0.38: return 1.0 - (u - 0.34) / 0.04 * 0.2
        if 0.38 <= u < 0.42: return -0.45
        if 0.52 < u < 0.66: return 0.22 * math.sin((u - 0.52) / 0.14 * math.pi)
        if 0.12 < u < 0.22: return 0.12 * math.sin((u - 0.12) / 0.10 * math.pi)
        return 0.0
    frames = []
    for f in range(N):
        head = f / N
        img = big((2, 10, 8))
        d = ImageDraw.Draw(img)
        for gx in range(0, W, 8):
            d.line([gx * SS, 0, gx * SS, H * SS], fill=(6, 30, 24), width=1)
        for gy in range(0, H, 8):
            d.line([0, gy * SS, W * SS, gy * SS], fill=(6, 30, 24), width=1)
        prev = None
        for x in range(W * 2):
            u = x / (W * 2)
            age = (head - u) % 1.0             # how long ago the sweep drew this point
            if age > 0.85:
                prev = None
                continue
            y = 36 - wave((u * 2) % 1.0) * 24
            k = (1 - age / 0.85) ** 1.6
            point = (u * W * SS, y * SS)
            if prev:
                d.line([prev, point], fill=(int(40 * k), int(255 * k), int(150 * k)), width=int(1.6 * SS))
            prev = point
        hx = head * W
        d.ellipse([(hx - 1.6) * SS, (36 - wave((head * 2) % 1.0) * 24 - 1.6) * SS, (hx + 1.6) * SS, (36 - wave((head * 2) % 1.0) * 24 + 1.6) * SS], fill=(220, 255, 235))
        frames.append(small(_lighter(img.filter(ImageFilter.GaussianBlur(2 * SS)).point(lambda v: int(v * 0.7)), img)))
    return frames


def pong():
    """A rally that never ends: two paddles tracking a ball, the loop closing where it began."""
    frames = []
    for f in range(N):
        t = f / N
        # Triangle waves give straight flight and exact bounces; one horizontal trip per half loop.
        tri = lambda u: 1 - abs((u % 2) - 1)
        bx = 9 + tri(t * 2) * 78
        by = 8 + tri(t * 4 + 0.3) * 48
        img = big((0, 0, 0))
        d = ImageDraw.Draw(img)
        for y in range(2, H, 8):
            d.rectangle([47 * SS, y * SS, 49 * SS, (y + 4) * SS], fill=(70, 70, 70))
        left = by if tri(t * 2) < 0.6 else 32 + (by - 32) * 0.4
        right = by if tri(t * 2) > 0.4 else 32 + (by - 32) * 0.4
        d.rectangle([4 * SS, (left - 8) * SS, 7 * SS, (left + 8) * SS], fill=(255, 255, 255))
        d.rectangle([89 * SS, (right - 8) * SS, 92 * SS, (right + 8) * SS], fill=(255, 255, 255))
        d.rectangle([(bx - 2) * SS, (by - 2) * SS, (bx + 2) * SS, (by + 2) * SS], fill=(255, 255, 255))
        frames.append(small(img))
    return frames


def rain():
    random.seed(33)
    cw, ch = 6, 8
    cols, rows = W // cw, H // ch
    glyphs = []
    for _ in range(20):
        g = Image.new("L", (4, 6), 0)
        px = g.load()
        for y in range(6):
            for x in range(4):
                px[x, y] = 255 if random.random() < 0.45 else 0
        glyphs.append(g)
    lanes = [(random.choice((1, 2)), random.randrange(rows * 2), random.randint(4, 8), [random.randrange(20) for _ in range(rows)]) for _ in range(cols)]
    frames = []
    for f in range(N):
        img = Image.new("RGB", (W, H), (0, 6, 2))
        for c, (speed, off, length, gl) in enumerate(lanes):
            period = rows * 2
            head = (off + f * speed * period // N) % period
            for r in range(rows):
                dist = (head - r) % period
                if dist < length:
                    k = 1 - dist / length
                    img.paste((190, 255, 200) if dist == 0 else (0, int(60 + 195 * k), int(20 + 50 * k)), (c * cw + 1, r * ch + 1), glyphs[gl[r]])
        frames.append(img)
    return frames


def aurora():
    frames = []
    for f in range(N):
        t = f / N
        img = Image.new("RGB", (W, H), (0, 4, 10))
        px = img.load()
        for x in range(W):
            for band, (color, base, amp, k, ph) in enumerate((((40, 255, 170), 16, 7, 1, 0.0), ((70, 150, 255), 26, 9, 2, 1.9), ((190, 90, 255), 8, 6, 1, 3.6))):
                top = base + math.sin(x / 96 * TAU * 1.5 + t * TAU * k + ph) * amp
                for y in range(max(0, int(top)), min(H, int(top) + 40)):
                    a = (1 - (y - top) / 40) ** 2.2 * (0.65 + 0.35 * math.sin(x * 1.9 + band * 2 + math.sin(t * TAU + x * 0.2))) * 0.7
                    r, g, b = px[x, y]
                    px[x, y] = (min(255, int(r + color[0] * a)), min(255, int(g + color[1] * a)), min(255, int(b + color[2] * a)))
        frames.append(img.filter(ImageFilter.GaussianBlur(0.5)))
    return frames


def wake():
    random.seed(35)
    motes = [(random.random(), random.uniform(0, TAU)) for _ in range(300)]
    frames = []
    for f in range(N):
        t = f / N
        glow = Image.new("L", (W * SS, H * SS), 0)
        d = ImageDraw.Draw(glow)
        for r, a in motes:
            arm = a + r * 5.5 - t * TAU
            x, y = 48 + math.cos(arm) * r * 46, 32 + math.sin(arm) * r * 29
            b = (1 - r * 0.7) * (0.55 + 0.45 * math.sin(t * TAU * 2 + a * 7))
            rad = 1.1 if r < 0.5 else 0.8
            d.ellipse([(x - rad) * SS, (y - rad) * SS, (x + rad) * SS, (y + rad) * SS], fill=int(255 * b))
        img = big((0, 5, 10))
        img.paste((0, 60, 140), mask=glow.filter(ImageFilter.GaussianBlur(2.4 * SS)).point(lambda v: min(255, int(v * 1.6))))
        img.paste((0, 184, 255), mask=glow)
        frames.append(small(img))
    return frames


STRIPS = {"equalizer": (equalizer, 10), "heartbeat": (heartbeat, 10), "pong": (pong, 10),
          "rain": (rain, 10), "aurora": (aurora, 10), "wake": (wake, 10)}
EYES = {"ping-pong": "directions/assigned.gif", "happy-mac": "directions/model-pick.gif", "six-pack": "directions/challenger-sixpack.gif",
        "companion": "directions/canon.gif", "tide-glow (on the keyboard now)": "directions/challenger-tideglow.gif"}


def main():
    OUT.mkdir(exist_ok=True)
    stills, cards = [], []
    for name, (make, fps) in STRIPS.items():
        frames = make()
        assert len(frames) <= 42
        frames[0].save(OUT / f"{name}.gif", save_all=True, append_images=frames[1:], duration=round(1000 / fps), loop=0, disposal=1)
        stills.append(frames[14])
        cards.append(f'<figure><img src="{name}.gif" alt="{name}"><figcaption>{name}</figcaption></figure>')
        print(f"{name}: {len(frames)} frames")
    eyes = "".join(f'<figure><img src="../{path}" alt="{name}"><figcaption>{name}</figcaption></figure>' for name, path in EYES.items())
    (OUT / "index.html").write_text(
        "<!doctype html><meta charset=utf-8><title>AL80 home strip</title><style>"
        "body{background:#0b0d10;color:#ddd;font:15px system-ui;margin:32px}h1{font-weight:600;font-size:20px;margin:28px 0 14px}"
        "main{display:flex;gap:24px;flex-wrap:wrap}figure{margin:0;text-align:center}"
        "img{width:384px;height:256px;image-rendering:pixelated;border-radius:6px;border:1px solid #2a2f36;display:block;margin-bottom:8px}"
        "</style><h1>Not eyes: animations for the home page strip, shown at 4x (96x64 on the keyboard)</h1><main>" + "".join(cards)
        + "</main><h1>Eyes</h1><main>" + eyes + "</main>")
    sheet = Image.new("RGB", (len(stills) * 200 + 8, 144), (40, 40, 40))
    for i, s in enumerate(stills):
        sheet.paste(s.resize((192, 128), Image.NEAREST), (8 + i * 200, 8))
    sheet.save(OUT / "sheet.png")


if __name__ == "__main__":
    main()
