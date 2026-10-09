#!/usr/bin/env python3
"""Render the candidate eye directions for the AL80 home strip (96x64, at most 42 frames).

    directions.py            writes screen/directions/<name>.gif and the decision-page comps

Every direction is rendered as the real artifact, at device resolution, so the choice is
made on what the panel will show. Shapes are drawn at 6x and scaled down.
"""
import math
import pathlib
import random

from PIL import Image, ImageChops, ImageDraw, ImageFilter

W, H, SS = 96, 64, 6
HERE = pathlib.Path(__file__).parent
OUT = HERE / "directions"
COMPS = HERE.parent / ".impeccable" / "mocks" / "decision"


def canvas(color):
    return Image.new("RGB", (W * SS, H * SS), color)


def down(image):
    return image.resize((W, H), Image.LANCZOS)


def ease(t):
    return t * t * (3 - 2 * t)


def box(cx, cy, rx, ry):
    return [(cx - rx) * SS, (cy - ry) * SS, (cx + rx) * SS, (cy + ry) * SS]


# ---- Ping-Pong: felt ground, two glossy white balls, pupils with weight ----

FELT, BALL, BALL_SHADE, INK = (226, 62, 36), (255, 255, 255), (214, 208, 196), (18, 16, 16)


def pingpong():
    # Pupil target per frame as (x, y) in -1..1, plus lid 0..1. Eyes may disagree.
    def both(x, y, lid=0.0):
        return ((x, y), (x, y), lid)

    score = (
        [both(0, 0)] * 3 + [both(-0.9, 0.1)] * 5 + [both(0.9, -0.1)] * 5
        + [((0.8, 0.2), (-0.8, 0.2), 0.0)] * 5                      # cross-eyed
        + [both(0, 0, 0.5), both(0, 0, 1.0), both(0, 0, 0.5)]        # blink
        + [both(math.cos(a) * 0.9, math.sin(a) * 0.9) for a in [i * math.pi / 5 - math.pi / 2 for i in range(11)]]  # eye roll
        + [both(0, -0.9)] * 3 + [both(0, 0.2, 0.55)] * 4 + [both(0, 0)] * 3
    )
    frames, pos = [], [[0.0, 0.0], [0.0, 0.0]]
    vel = [[0.0, 0.0], [0.0, 0.0]]
    for left, right, lid in score:
        img = canvas(FELT)
        draw = ImageDraw.Draw(img)
        for i, (target, cx) in enumerate(((left, 28.5), (right, 67.5))):
            for axis in (0, 1):  # a spring gives the pupil overshoot and settle
                vel[i][axis] = vel[i][axis] * 0.42 + (target[axis] - pos[i][axis]) * 0.62
                pos[i][axis] += vel[i][axis]
            cy, r = 32, 21
            draw.ellipse(box(cx + 1.2, cy + 2.2, r, r), fill=(150, 34, 20))          # drop shadow on felt
            draw.ellipse(box(cx, cy, r, r), fill=BALL_SHADE)
            draw.ellipse(box(cx - 1.4, cy - 1.8, r - 2.2, r - 2.2), fill=BALL)      # lit face
            px, py = cx + pos[i][0] * 10.5, cy + pos[i][1] * 10.5
            draw.ellipse(box(px, py, 8.2, 8.2), fill=INK)
            draw.ellipse(box(px - 2.6, py - 2.8, 2.1, 2.1), fill=BALL)              # catch light
            if lid > 0:  # a felt lid comes down over the ball
                top = cy - r - 1
                draw.chord(box(cx, cy, r + 1.2, r + 1.2), 180, 360, fill=FELT) if lid >= 0.5 else None
                draw.rectangle([(cx - r - 2) * SS, top * SS, (cx + r + 2) * SS, (top + (2 * r + 2) * lid) * SS], fill=FELT)
                draw.line([(cx - r) * SS, (top + (2 * r + 2) * lid) * SS, (cx + r) * SS, (top + (2 * r + 2) * lid) * SS], fill=(150, 34, 20), width=SS)
        frames.append(down(img))
    return frames, 10


# ---- Happy Mac: one-bit, drawn on a coarse grid ----

def happymac():
    cell = 3  # one logical pixel is 3x3 device pixels: chunky, legible at arm's length
    gw, gh = W // cell, H // cell

    def face(eye_dx=0, eye_h=6, mouth=0, wink=False, dither=0.0):
        bits = [[0] * gw for _ in range(gh)]

        def rect(x, y, w, h):
            for yy in range(y, y + h):
                for xx in range(x, x + w):
                    if 0 <= xx < gw and 0 <= yy < gh:
                        bits[yy][xx] = 1

        for side, x in ((0, 8), (1, 21)):            # eyes: tall blocks
            h = 1 if (wink and side == 1) else eye_h
            rect(x + eye_dx, 3 + (6 - h), 3, h)
        rect(16, 5, 1, 7)                            # nose: an L
        rect(14, 11, 3, 1)
        curve = {0: [0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0], 1: [0, 2, 3, 3, 3, 3, 3, 3, 3, 3, 2, 0]}[mouth]
        for i, dy in enumerate(curve):               # mouth
            rect(10 + i, 14 + dy, 1, 1)
        img = Image.new("RGB", (W, H), (0, 0, 0))
        px = img.load()
        for y in range(gh):
            for x in range(gw):
                on = bits[y][x]
                if dither and ((x + y) % 2 == 0) and random.random() < dither:
                    on = 1 - on
                if on:
                    for dy in range(cell):
                        for dx in range(cell):
                            px[x * cell + dx, y * cell + dy] = (255, 255, 255)
        return img

    random.seed(7)
    f = face
    frames = (
        [f()] * 5 + [f(eye_h=3), f(eye_h=1), f(eye_h=3)] + [f()] * 3
        + [f(eye_dx=-1)] * 5 + [f()] * 2 + [f(eye_dx=1)] * 5 + [f()] * 2
        + [f(mouth=1)] * 2 + [f(mouth=1, wink=True)] * 4 + [f(mouth=1)] * 2
        + [f(dither=0.35), f(dither=0.8), f(dither=0.35)] + [f()] * 3
    )
    return frames, 6


# ---- Tide Glow: eyes made of living light on a black sea ----

def tideglow():
    random.seed(11)
    motes = []
    for side, cx in ((-1, 26), (1, 70)):
        for _ in range(230):
            a, r = random.uniform(0, 2 * math.pi), math.sqrt(random.random())
            motes.append({"hx": cx + math.cos(a) * 19 * r, "hy": 32 + math.sin(a) * 15 * r, "cx": cx,
                          "ph": random.uniform(0, 6.28), "sp": random.uniform(0.5, 1.6), "edge": r,
                          "vx": random.uniform(-1, 1), "vy": random.uniform(0.2, 1.4)})
    n = 40
    # Blink: the eyes close to a line, the light spills downward as a wake, then gathers again.
    def openness(i):
        if 14 <= i <= 22:
            return [0.6, 0.2, 0.0, 0.0, 0.0, 0.15, 0.4, 0.7, 0.9][i - 14]
        return 1.0
    frames = []
    for i in range(n):
        o = openness(i)
        spill = max(0.0, min(1.0, (i - 15) / 3.0)) * max(0.0, 1 - max(0, i - 19) / 5.0)
        look = math.sin(i / n * 2 * math.pi) * 5 if not 12 <= i <= 24 else 0
        glow = Image.new("L", (W * SS, H * SS), 0)
        hot = Image.new("L", (W * SS, H * SS), 0)
        dg, dh = ImageDraw.Draw(glow), ImageDraw.Draw(hot)
        for m in motes:
            t = i / n * 2 * math.pi
            x = m["hx"] + math.sin(t * 2 + m["ph"]) * 0.9 + look * (1.2 - m["edge"])
            y = 32 + (m["hy"] - 32) * max(o, 0.06) + math.cos(t * 3 + m["ph"]) * 0.6
            x += m["vx"] * spill * 7
            y += m["vy"] * spill * 13
            tw = 0.55 + 0.45 * math.sin(t * 4 * m["sp"] + m["ph"])
            b = int(255 * (0.35 + 0.65 * tw) * (1 - 0.4 * m["edge"]) * (1 - 0.55 * spill))
            r = 0.9 if m["edge"] > 0.7 else 1.3
            dg.ellipse(box(x, y, r, r), fill=max(b, dg.im.getpixel((min(max(int(x * SS), 0), W * SS - 1), min(max(int(y * SS), 0), H * SS - 1)))))
            if tw > 0.8 and m["edge"] < 0.6:
                dh.ellipse(box(x, y, 0.5, 0.5), fill=255)
        halo = glow.filter(ImageFilter.GaussianBlur(2.6 * SS))
        img = canvas((0, 6, 12))
        img.paste((0, 70, 150), mask=halo.point(lambda v: min(255, int(v * 1.5))))
        img.paste((0, 184, 255), mask=glow)
        img.paste((170, 255, 236), mask=hot)
        frames.append(down(img))
    return frames, 10


# ---- Six-Pack: two instrument dials whose needles do the looking ----

PANEL, FACE, MARK, NEEDLE, LAMP = (9, 11, 10), (20, 23, 22), (242, 242, 232), (150, 255, 84), (255, 176, 0)


def sixpack():
    def both(a, shutter=0.0, lamp=False):
        return (a, a, shutter, lamp)

    score = (
        [both(-90)] * 3 + [both(-150)] * 5 + [both(-30)] * 5 + [(-150, -30, 0.0, True)] * 4   # disagreement lights the lamp
        + [both(-90, 0.5), both(-90, 1.0), both(-90, 0.5)]
        + [both(-90)] * 2 + [both(-60)] * 3 + [both(-120)] * 3 + [both(-90)] * 2
        + [both(-90 + i * 45) for i in range(1, 9)]                                           # one full sweep
        + [both(-90)] * 3
    )
    frames, ang, vel = [], [-90.0, -90.0], [0.0, 0.0]
    for left, right, shutter, lamp in score:
        img = canvas(PANEL)
        draw = ImageDraw.Draw(img)
        glow = Image.new("L", img.size, 0)
        dglow = ImageDraw.Draw(glow)
        for i, (target, cx) in enumerate(((left, 25), (right, 71))):
            vel[i] = vel[i] * 0.5 + (target - ang[i]) * 0.42     # damped, physical needle
            ang[i] += vel[i]
            cy, r = 32, 20.5
            draw.ellipse(box(cx, cy, r + 1.6, r + 1.6), fill=(46, 50, 48))   # bezel
            draw.ellipse(box(cx, cy, r, r), fill=FACE)
            for k in range(24):                                              # tick ring
                a = math.radians(k * 15)
                long = k % 3 == 0
                r0 = r - (5.6 if long else 3.2)
                draw.line([(cx + math.cos(a) * r0) * SS, (cy + math.sin(a) * r0) * SS,
                           (cx + math.cos(a) * (r - 1.2)) * SS, (cy + math.sin(a) * (r - 1.2)) * SS],
                          fill=MARK, width=int((1.5 if long else 0.8) * SS))
            a = math.radians(ang[i])
            tip = ((cx + math.cos(a) * (r - 6.5)) * SS, (cy + math.sin(a) * (r - 6.5)) * SS)
            tail = ((cx - math.cos(a) * 4.5) * SS, (cy - math.sin(a) * 4.5) * SS)
            for d in (draw, dglow):
                d.line([tail, tip], fill=NEEDLE if d is draw else 255, width=int(2.0 * SS))
            draw.ellipse(box(cx, cy, 3.1, 3.1), fill=(60, 64, 62))
            draw.ellipse(box(cx, cy, 1.3, 1.3), fill=PANEL)
            if shutter > 0:  # the instrument's flag drops across the glass
                draw.chord(box(cx, cy, r, r), 180, 360, fill=(30, 33, 32)) if shutter >= 0.5 else None
                draw.rectangle([(cx - r) * SS, (cy - r) * SS, (cx + r) * SS, (cy - r + 2 * r * shutter) * SS], fill=(30, 33, 32))
                glow.paste(0, [int((cx - r) * SS), int((cy - r) * SS), int((cx + r) * SS), int((cy - r + 2 * r * shutter) * SS)])
        if lamp:
            draw.ellipse(box(48, 8, 2.6, 2.6), fill=LAMP)
            dglow.ellipse(box(48, 8, 2.6, 2.6), fill=160)
        halo = Image.new("RGB", img.size, (0, 0, 0))
        halo.paste((70, 150, 30), mask=glow.filter(ImageFilter.GaussianBlur(2.2 * SS)))
        frames.append(down(ImageChops.lighter(img, halo) if not lamp else ImageChops.lighter(img, halo)))
    return frames, 8


# ---- Companion: the category standard, rounded robot eyes, played straight ----

def companion():
    cyan, core = (0, 210, 255), (190, 245, 255)

    def pose(look=0.0, openness=1.0):
        mask = Image.new("L", (W * SS, H * SS), 0)
        d = ImageDraw.Draw(mask)
        for side in (-1, 1):
            w, h = 24, max(3.0, 32 * openness)
            cx = 48 + side * 20 + look * 6
            d.rounded_rectangle(box(cx, 32, w / 2, h / 2), radius=min(8 * SS, h * SS / 2), fill=255)
        img = canvas((0, 0, 0))
        bloom = canvas((0, 0, 0))
        bloom.paste(cyan, mask=mask.filter(ImageFilter.GaussianBlur(3.2 * SS)).point(lambda v: int(v * 0.45)))
        img.paste(cyan, mask=mask)
        img.paste(core, mask=mask.filter(ImageFilter.MinFilter(9 * SS + 1)).filter(ImageFilter.GaussianBlur(3 * SS)).point(lambda v: int(v * 0.7)))
        return down(ImageChops.lighter(bloom, img))

    frames = ([pose()] * 6 + [pose(0, 0.45), pose(0, 0.06), pose(0, 0.5)] + [pose()] * 3 + [pose(-0.5)] + [pose(-1)] * 5
              + [pose()] * 3 + [pose(0.5)] + [pose(1)] * 5 + [pose()] * 4)
    return frames, 6


DIRECTIONS = {"assigned": pingpong, "model-pick": happymac, "challenger-tideglow": tideglow,
              "challenger-sixpack": sixpack, "canon": companion}


def main():
    OUT.mkdir(exist_ok=True)
    COMPS.mkdir(parents=True, exist_ok=True)
    sheet_rows = []
    for name, make in DIRECTIONS.items():
        frames, fps = make()
        assert len(frames) <= 42, (name, len(frames))
        frames[0].save(OUT / f"{name}.gif", save_all=True, append_images=frames[1:], duration=round(1000 / fps), loop=0, disposal=1)
        # The comp is the same animation at 6x with hard pixels, as the panel shows it.
        big = [f.resize((W * 6, H * 6), Image.NEAREST) for f in frames]
        big[0].save(COMPS / f"{name}.webp", save_all=True, append_images=big[1:], duration=round(1000 / fps), loop=0, lossless=True)
        sheet_rows.append([frames[i] for i in (0, len(frames) // 5, 2 * len(frames) // 5, 3 * len(frames) // 5, 4 * len(frames) // 5)])
        print(f"{name}: {len(frames)} frames at {fps} fps")
    sheet = Image.new("RGB", (5 * (W * 3 + 6) + 6, len(sheet_rows) * (H * 3 + 6) + 6), (40, 40, 40))
    for r, row in enumerate(sheet_rows):
        for c, f in enumerate(row):
            sheet.paste(f.resize((W * 3, H * 3), Image.NEAREST), (6 + c * (W * 3 + 6), 6 + r * (H * 3 + 6)))
    sheet.save(OUT / "sheet.png")


if __name__ == "__main__":
    main()
