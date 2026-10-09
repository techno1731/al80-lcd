#!/usr/bin/env python3
"""Render "Lookout", the animated eyes for the AL80 home page strip (96x64).

    eyes.py [out.gif]

The strip holds at most 42 frames at one global frame rate, so the loop is scored
frame by frame: every frame is a pose, and holds are spent only where they read as life.
Shapes are drawn at 8x and scaled down, which gives clean edges on a 96-pixel panel.
"""
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

W, H, SS = 96, 64, 8
FPS = 6
AMBER = (255, 176, 0)       # body of the eye
CORE = (255, 226, 150)      # hot centre
BLACK = (0, 0, 0)

EYE_W, EYE_H, GAP, RADIUS = 25, 33, 16, 9

# One pose per frame: (look -1..1, openness 0..1, smile 0..1, lift in px).
REST = (0.0, 1.0, 0.0, 0)
BREATH = (0.0, 1.0, 0.0, -1)


def blink():
    return [(0.0, 0.45, 0.0, 0), (0.0, 0.06, 0.0, 0), (0.0, 0.5, 0.0, 0)]


def glance(side, hold):
    return [(0.55 * side, 0.92, 0.0, 0)] + [(1.0 * side, 0.92, 0.0, 0)] * hold + [(0.4 * side, 1.0, 0.0, 0)]


SCORE = (
    [REST, REST, BREATH, BREATH, REST] + blink()
    + [REST, BREATH] + glance(-1, 4)
    + [REST] + blink() + blink()
    + [BREATH] + glance(1, 3)
    + [REST, (0.0, 0.8, 0.6, 0), (0.0, 0.66, 1.0, 1), (0.0, 0.66, 1.0, 0), (0.0, 0.66, 1.0, 1), (0.0, 0.66, 1.0, 1), (0.0, 0.82, 0.55, 0)]
    + [REST, BREATH, REST]
)
assert len(SCORE) <= 42, len(SCORE)


def eye_mask(look, openness, smile, lift, side):
    """Mask of one eye at 8x. side is -1 for the left eye, 1 for the right."""
    mask = Image.new("L", (W * SS, H * SS), 0)
    draw = ImageDraw.Draw(mask)
    # The eye on the side being looked at grows a little; the other shrinks.
    scale = 1.0 + 0.08 * look * side
    w = EYE_W * scale
    h = max(3.0, EYE_H * scale * openness)
    w *= 1.0 + 0.12 * (1.0 - openness)  # a closing eye spreads
    cx = W / 2 + side * (GAP + EYE_W) / 2 + look * 6
    cy = H / 2 + lift + (EYE_H * scale - h) * 0.12
    box = [(cx - w / 2) * SS, (cy - h / 2) * SS, (cx + w / 2) * SS, (cy + h / 2) * SS]
    draw.rounded_rectangle(box, radius=min(RADIUS * SS, h * SS / 2), fill=255)
    if smile > 0:
        # A cheek rises from below and bites the lower part away, leaving an arch.
        bite = h * 0.7 * smile
        rx = w * 0.62
        draw.ellipse([(cx - rx) * SS, (cy + h / 2 - bite) * SS, (cx + rx) * SS, (cy + h / 2 + bite * 1.2) * SS], fill=0)
    return mask


def frame(pose):
    look, openness, smile, lift = pose
    mask = ImageChops.lighter(eye_mask(look, openness, smile, lift, -1), eye_mask(look, openness, smile, lift, 1))
    body = Image.new("RGB", mask.size, BLACK)
    body.paste(AMBER, mask=mask)
    # Hot centre: the same shape eroded and blurred, so the light falls off toward the rim.
    core = mask.filter(ImageFilter.MinFilter(9 * SS + 1)).filter(ImageFilter.GaussianBlur(3.0 * SS))
    body.paste(CORE, mask=core.point(lambda v: int(v * 0.7)))
    # Phosphor bloom on the black around the shape.
    glow = mask.filter(ImageFilter.GaussianBlur(3.5 * SS)).point(lambda v: int(v * 0.42))
    bloom = Image.new("RGB", mask.size, BLACK)
    bloom.paste(AMBER, mask=glow)
    return ImageChops.lighter(bloom, body).resize((W, H), Image.LANCZOS)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "lookout.gif"
    frames = [frame(pose) for pose in SCORE]
    frames[0].save(out, save_all=True, append_images=frames[1:], duration=round(1000 / FPS), loop=0, disposal=1)
    # Contact sheet for review: every frame at 3x.
    cols = 7
    rows = -(-len(frames) // cols)
    sheet = Image.new("RGB", (cols * (W * 3 + 6) + 6, rows * (H * 3 + 6) + 6), (40, 40, 40))
    for i, f in enumerate(frames):
        sheet.paste(f.resize((W * 3, H * 3), Image.NEAREST), (6 + (i % cols) * (W * 3 + 6), 6 + (i // cols) * (H * 3 + 6)))
    sheet.save(out.rsplit(".", 1)[0] + "-sheet.png")
    print(f"{len(frames)} frames at {FPS} fps, {len(frames) / FPS:.1f} s loop -> {out}")


if __name__ == "__main__":
    main()
