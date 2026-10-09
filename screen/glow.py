#!/usr/bin/env python3
"""Full-screen scenes in the Tide Glow world: living light on black water (96x160, seamless loops).

    glow.py                  writes screen/glow/<name>.gif, a gallery page and a review sheet
"""
import math
import pathlib
import random

from PIL import Image, ImageDraw, ImageFilter

W, H, SS, N = 96, 160, 4, 40
OUT = pathlib.Path(__file__).parent / "glow"
TAU = 2 * math.pi


def compose(dots, deep, body, hot, blur=2.4):
    """dots: (x, y, radius, brightness 0-1). Light is drawn large, blurred into a halo, then scaled."""
    glow = Image.new("L", (W * SS, H * SS), 0)
    core = Image.new("L", (W * SS, H * SS), 0)
    dg, dc = ImageDraw.Draw(glow), ImageDraw.Draw(core)
    for x, y, r, b in dots:
        if b <= 0:
            continue
        box = [(x - r) * SS, (y - r) * SS, (x + r) * SS, (y + r) * SS]
        dg.ellipse(box, fill=int(255 * min(1.0, b)))
        if b > 0.8:
            dc.ellipse([(x - r * 0.45) * SS, (y - r * 0.45) * SS, (x + r * 0.45) * SS, (y + r * 0.45) * SS], fill=255)
    img = Image.new("RGB", (W * SS, H * SS), (0, 5, 10))
    img.paste(deep, mask=glow.filter(ImageFilter.GaussianBlur(blur * SS)).point(lambda v: min(255, int(v * 1.6))))
    img.paste(body, mask=glow)
    img.paste(hot, mask=core)
    return img.resize((W, H), Image.LANCZOS)


def jellyfish():
    random.seed(21)
    rim = [random.uniform(0, TAU) for _ in range(70)]
    trails = [(random.uniform(-1, 1), random.uniform(0, 1), random.uniform(0, TAU)) for _ in range(120)]
    frames = []
    for f in range(N):
        t = f / N
        pulse = 0.5 + 0.5 * math.sin(t * TAU)          # bell opens and closes once per loop
        cy = 62 - 10 * math.sin(t * TAU - 0.9)         # and rises on each stroke
        bw, bh = 24 + 9 * pulse, 20 - 5 * pulse
        dots = []
        for a in rim:                                  # the bell: light gathered on a dome
            for k in (1.0, 0.72, 0.42):
                x = 48 + math.cos(a) * bw * k
                y = cy - abs(math.sin(a)) * bh * k
                dots.append((x, y, 1.3, 0.45 + 0.55 * k * (0.6 + 0.4 * math.sin(a * 3 + t * TAU * 2))))
        for u, v, ph in trails:                        # tentacles: motes trailing below, swaying
            y = cy + 4 + v * 78
            sway = math.sin(t * TAU + v * 4 + ph) * (3 + v * 9)
            x = 48 + u * bw * (0.85 - v * 0.45) + sway
            dots.append((x, y, 0.9, (1 - v) * (0.5 + 0.5 * math.sin(t * TAU * 3 + ph))))
        frames.append(compose(dots, (60, 0, 130), (190, 80, 255), (245, 210, 255)))
    return frames


def wake():
    random.seed(22)
    motes = [(random.random(), random.uniform(0, TAU), random.uniform(0.6, 1.4)) for _ in range(420)]
    frames = []
    for f in range(N):
        t = f / N
        dots = []
        for r, a, sp in motes:
            arm = a + r * 5.5 - t * TAU                 # two spiral arms turning once per loop
            x = 48 + math.cos(arm) * r * 44
            y = 80 + math.sin(arm) * r * 70
            b = (1 - r * 0.75) * (0.55 + 0.45 * math.sin(t * TAU * 2 * round(sp * 2) / 2 + a * 7))
            dots.append((x, y, 1.1 if r < 0.5 else 0.8, b))
        frames.append(compose(dots, (0, 60, 140), (0, 184, 255), (180, 255, 240)))
    return frames


def fireflies():
    random.seed(23)
    flies = [(random.uniform(6, 90), random.uniform(0, 160), random.uniform(0, TAU), random.choice((1, 2)), random.uniform(3, 9)) for _ in range(46)]
    frames = []
    for f in range(N):
        t = f / N
        dots = []
        for x0, y0, ph, k, amp in flies:
            x = x0 + math.sin(t * TAU * k + ph) * amp
            y = (y0 - t * 160 * k) % 160                # drifting upward, wrapping
            blink = max(0.0, math.sin(t * TAU * k * 2 + ph)) ** 2
            dots.append((x, y, 1.5, 0.15 + 0.85 * blink))
        frames.append(compose(dots, (70, 90, 0), (190, 255, 60), (250, 255, 200), blur=3.0))
    return frames


def aurora():
    frames = []
    for f in range(N):
        t = f / N
        img = Image.new("RGB", (W, H), (0, 4, 10))
        px = img.load()
        for x in range(W):
            for band, (color, base, amp, k, ph) in enumerate((((40, 255, 170), 46, 16, 1, 0.0), ((70, 150, 255), 70, 20, 2, 1.9), ((190, 90, 255), 30, 12, 1, 3.6))):
                top = base + math.sin(x / 96 * TAU * 1.5 + t * TAU * k + ph) * amp + math.sin(x / 96 * TAU * 4 - t * TAU + ph) * 4
                for y in range(int(top), min(H, int(top) + 86)):
                    if y < 0:
                        continue
                    d = (y - top) / 86
                    ray = 0.65 + 0.35 * math.sin(x * 1.9 + band * 2 + math.sin(t * TAU + x * 0.2))
                    a = (1 - d) ** 2.2 * ray * 0.7
                    r, g, b = px[x, y]
                    px[x, y] = (min(255, int(r + color[0] * a)), min(255, int(g + color[1] * a)), min(255, int(b + color[2] * a)))
        frames.append(img.filter(ImageFilter.GaussianBlur(0.6)))
    return frames


def lookout():
    """The home page eyes at full height: the same motes, slowly breathing."""
    random.seed(11)
    motes = []
    for cx in (27, 69):
        for _ in range(300):
            a, r = random.uniform(0, TAU), math.sqrt(random.random())
            motes.append((cx + math.cos(a) * 19 * r, 78 + math.sin(a) * 30 * r, r, random.uniform(0, TAU), random.choice((1, 2, 3)), random.uniform(-1, 1), random.uniform(0.3, 1.4)))
    frames = []
    for f in range(N):
        t = f / N
        o = 1.0
        if 16 <= f <= 23:
            o = [0.55, 0.15, 0.0, 0.0, 0.2, 0.5, 0.8, 0.95][f - 16]
        spill = max(0.0, min(1.0, (f - 17) / 3.0)) * max(0.0, 1 - max(0, f - 20) / 5.0)
        dots = []
        for hx, hy, edge, ph, k, vx, vy in motes:
            x = hx + math.sin(t * TAU * k + ph) * 1.1 + vx * spill * 9
            y = 78 + (hy - 78) * max(o, 0.05) + math.cos(t * TAU * k + ph) * 0.9 + vy * spill * 34
            b = (0.35 + 0.65 * (0.5 + 0.5 * math.sin(t * TAU * k * 2 + ph))) * (1 - 0.4 * edge) * (1 - 0.5 * spill)
            dots.append((x, y, 1.25 if edge < 0.7 else 0.9, b))
        frames.append(compose(dots, (0, 70, 150), (0, 184, 255), (170, 255, 236)))
    return frames


SCENES = {"jellyfish": (jellyfish, 10), "wake": (wake, 10), "fireflies": (fireflies, 10), "aurora": (aurora, 10), "big-eyes": (lookout, 10)}


def main():
    OUT.mkdir(exist_ok=True)
    cards, stills = [], []
    for name, (make, fps) in SCENES.items():
        frames = make()
        frames[0].save(OUT / f"{name}.gif", save_all=True, append_images=frames[1:], duration=round(1000 / fps), loop=0, disposal=1)
        cards.append(f'<figure><img src="{name}.gif" alt="{name}"><figcaption>{name}</figcaption></figure>')
        stills.append(frames[10])
        print(f"{name}: {len(frames)} frames at {fps} fps")
    (OUT / "index.html").write_text(
        "<!doctype html><meta charset=utf-8><title>AL80 glow scenes</title><style>"
        "body{background:#0b0d10;color:#ddd;font:15px system-ui;margin:32px}h1{font-weight:600;font-size:20px}"
        "main{display:flex;gap:28px;flex-wrap:wrap}figure{margin:0;text-align:center}"
        "img{width:288px;height:480px;image-rendering:pixelated;border-radius:6px;border:1px solid #2a2f36;display:block;margin-bottom:10px}"
        "</style><h1>Full-screen scenes in the Tide Glow style, shown at 3x (96x160 on the keyboard)</h1><main>" + "".join(cards) + "</main>")
    sheet = Image.new("RGB", (len(stills) * 200 + 8, 336), (40, 40, 40))
    for i, s in enumerate(stills):
        sheet.paste(s.resize((192, 320), Image.NEAREST), (8 + i * 200, 8))
    sheet.save(OUT / "sheet.png")


if __name__ == "__main__":
    main()
