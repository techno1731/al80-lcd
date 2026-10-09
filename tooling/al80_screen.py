#!/usr/bin/env python3
"""Drive the YUNZII AL80 screen from the command line.

    al80_screen.py gif <file>... [--where page|home|boot] [--fps N] [--fit cover|contain] [--no-theme]
    al80_screen.py picture <file> [--fit cover|contain]
    al80_screen.py view home|gif|picture
    al80_screen.py clock
    al80_screen.py clear gif|pictures

Several GIF files become one medley: the slot's frames are shared out evenly and the scenes
play one after another. With our firmware the key backlight then takes each scene's dominant
colour in step with the animation (--no-theme leaves the backlight alone).

Needs the keyboard on USB, and the hidapi and Pillow packages. Works with the factory
USB identity and with the Apple identity our firmware presents in Mac mode.
"""
import argparse
import colorsys
import datetime
import sys
import time

PANEL_W = 96
IDS = ((0x28E9, 0x30AF), (0x05AC, 0x029C))
RAW_USAGE = (0xFF60, 0x61)

# Where a GIF can live: mode byte, height in pixels, frame limit.
GIF_TARGETS = {"boot": (0, 160, 64), "page": (1, 160, 160), "home": (2, 64, 42)}

OP_ANNOUNCE, OP_DATA, OP_FINISH = 0x40, 0x41, 0x42
# Display module commands (the PK_* set).
PK_TIME, PK_DATE = 0x09, 0x0A
PK_GO_HOME, PK_ADD_PIC, PK_NEXT_PIC, PK_DEL_PIC, PK_GO_GIF = 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
PK_GUI_EVENT, PK_FRAME_LEN, PK_GIF_NUM, PK_GIF_FRAME = 0x10, 0x11, 0x12, 0x13

OP_THEME_SET = 0x50  # our firmware: backlight colour per GIF scene
THEME_MAX = 8

CHUNK = 56     # payload bytes per report
BANK = 1024    # GIF frames are written to the module in banks of this size


# ---- packets (pure functions, covered by tooling/test_al80_screen.py) ----

def crc16_modbus(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def report(op, payload=(), offset=0, length=None):
    """One 64-byte report: op, offset (LE), length, additive checksum (LE), 0, payload."""
    payload = list(payload)
    length = len(payload) if length is None else length
    head = [op, offset & 0xFF, offset >> 8, length]
    checksum = (sum(head) + sum(payload)) & 0xFFFF
    body = head + [checksum & 0xFF, checksum >> 8, 0] + payload
    return body + [0] * (64 - len(body))


def command(kind, data=(), declared=None):
    """A display module command: A5 5A, kind, length (BE), CRC over those three, inline data."""
    declared = len(data) if declared is None else declared
    head = [kind, declared >> 8, declared & 0xFF]
    crc = crc16_modbus(head)
    return [0xA5, 0x5A] + head + [crc >> 8, crc & 0xFF] + list(data)


FINISH = report(OP_FINISH, length=0x38)


def data_reports(blob, bank=None):
    """Split pixel bytes into data reports. With a bank size, offsets restart in every bank."""
    out = []
    step = bank or len(blob)
    for base in range(0, len(blob), step):
        part = blob[base:base + step]
        for off in range(0, len(part), CHUNK):
            out.append(report(OP_DATA, part[off:off + CHUNK], offset=off))
    return out


def rgb565_be(image):
    """Pixels of an RGB image as RGB565, high byte first, row by row."""
    raw = image.tobytes()
    out = bytearray()
    for i in range(0, len(raw), 3):
        value = ((raw[i] >> 3) << 11) | ((raw[i + 1] >> 2) << 5) | (raw[i + 2] >> 3)
        out += bytes((value >> 8, value & 0xFF))
    return bytes(out)


def fill_scene(frames, count):
    """Exactly `count` frames from a clip: thinned evenly when it is longer, looped when shorter."""
    if len(frames) > count:
        return [frames[round(i * (len(frames) - 1) / (count - 1))] for i in range(count)] if count > 1 else frames[:1]
    return [frames[i % len(frames)] for i in range(count)]


def dominant_hue_sat(rgb_frames):
    """Dominant vivid colour of a scene as QMK hue and saturation (0-255 each).

    Pixels vote with their saturation times brightness, so a dark or grey background does not
    wash out the answer. Hue is averaged on the colour wheel."""
    import math
    x = y = weight = sat_sum = 0.0
    for raw in rgb_frames:
        for i in range(0, len(raw), 3 * 7):  # every seventh pixel is plenty
            h, s, v = colorsys.rgb_to_hsv(raw[i] / 255, raw[i + 1] / 255, raw[i + 2] / 255)
            w = s * v
            x += math.cos(h * 2 * math.pi) * w
            y += math.sin(h * 2 * math.pi) * w
            sat_sum += s * w
            weight += w
    if weight < 1e-6:
        return 0, 0  # a colourless scene: white light
    hue = (math.atan2(y, x) / (2 * math.pi)) % 1.0
    return round(hue * 255) % 256, max(140, min(255, round(sat_sum / weight * 255)))


def theme_report(scenes):
    """The firmware's theme packet: scenes are (seconds, hue, sat)."""
    body = [OP_THEME_SET, len(scenes)]
    for seconds, hue, sat in scenes:
        body += [max(1, min(255, round(seconds * 10))), hue, sat]
    return body + [0] * (64 - len(body))


def gif_stream(frames, mode, fps):
    """Every report of a GIF upload, each paired with the pause the module needs after it."""
    steps = [(report(OP_ANNOUNCE, command(PK_GIF_NUM, [mode, 0])), 0.03),
             (report(OP_DATA, command(PK_GIF_FRAME, [mode, 0])), 0.03)]
    for index, frame in enumerate(frames):
        # The module writes its flash on the first and every 16th frame.
        steps.append((report(OP_DATA, command(PK_GUI_EVENT, [0x02, mode, index])), 3.0 if index % 16 == 0 else 0.03))
        steps.append((report(OP_DATA, command(PK_FRAME_LEN, declared=len(frame))), 0.0))
        for base in range(0, len(frame), BANK):
            bank = data_reports(frame[base:base + BANK])
            steps += [(r, 0.0) for r in bank[:-1]] + [(bank[-1], 0.03)]
    steps += [(report(OP_DATA, command(PK_GIF_NUM, [mode, len(frames)])), 0.03),
              (report(OP_DATA, command(PK_GIF_FRAME, [mode, fps])), 0.03),
              (FINISH, 0.03)]
    return steps


def picture_stream(pixels):
    """Every report of a still picture upload. The module shows the picture when it lands."""
    steps = [(report(OP_ANNOUNCE, command(PK_GUI_EVENT, [0x01])), 0.3),
             (report(OP_DATA, command(PK_ADD_PIC, declared=len(pixels))), 0.03)]
    steps += [(r, 0.0) for r in data_reports(pixels)]
    return steps + [(FINISH, 0.03)]


def simple_stream(kind, data=()):
    """A command with no pixel data, such as a view switch."""
    return [(report(OP_ANNOUNCE, command(kind, data)), 0.03), (FINISH, 0.03)]


def clock_stream(now):
    time_of_day = [now.hour, now.minute, now.second]
    date = [now.year % 100, now.isoweekday(), now.month, now.day]
    steps = []
    for kind, values in ((PK_TIME, time_of_day), (PK_DATE, date)):
        steps += [(report(OP_ANNOUNCE, command(kind, declared=len(values))), 0.06),
                  (report(OP_DATA, values), 0.06), (FINISH, 0.06)]
    return steps * 3  # the vendor app sends the pair three times


# ---- images ----

def load_frames(path, height, fit, limit):
    from PIL import Image, ImageOps, ImageSequence
    source = Image.open(path)
    frames, durations = [], []
    for frame in ImageSequence.Iterator(source):
        durations.append(frame.info.get("duration", 0))
        rgb = frame.convert("RGBA")
        canvas = Image.new("RGBA", rgb.size, (0, 0, 0, 255))
        canvas.alpha_composite(rgb)
        fitted = (ImageOps.fit if fit == "cover" else ImageOps.pad)(canvas.convert("RGB"), (PANEL_W, height), Image.LANCZOS)
        frames.append(fitted.tobytes())
    if len(frames) > limit:
        # Keep the whole animation by dropping frames evenly.
        keep = [round(i * (len(frames) - 1) / (limit - 1)) for i in range(limit)]
        scale = len(frames) / limit
        frames = [frames[i] for i in keep]
        durations = [d * scale for d in durations[:limit]]
    mean_ms = sum(durations) / len(durations) if durations and sum(durations) else 100
    return frames, max(1, min(60, round(1000 / mean_ms)))


def rgb565_bytes(raw):
    """RGB888 bytes to RGB565, high byte first."""
    out = bytearray()
    for i in range(0, len(raw), 3):
        value = ((raw[i] >> 3) << 11) | ((raw[i + 1] >> 2) << 5) | (raw[i + 2] >> 3)
        out += bytes((value >> 8, value & 0xFF))
    return bytes(out)


# ---- device ----

def open_device():
    import hid
    for vid, pid in IDS:
        for info in hid.enumerate(vid, pid):
            if (info["usage_page"], info["usage"]) == RAW_USAGE:
                device = hid.device()
                device.open_path(info["path"])
                return device
    sys.exit("AL80 not found on USB (close VIA or the vendor screen site if they are open)")


def send(steps, label=None):
    device = open_device()
    try:
        total = len(steps)
        for count, (packet, pause) in enumerate(steps, 1):
            device.write([0x00] + packet)
            try:
                reply = device.read(64, 500)  # each report is echoed back with an ACK byte
            except OSError:
                sys.exit(f"lost the keyboard at report {count} of {total} (unplugged, or a KVM switched away): run it again")
            if not reply:
                sys.exit(f"keyboard stopped answering at report {count} of {total}")
            if pause:
                time.sleep(pause)
            if label and (count % 200 == 0 or count == total):
                print(f"\r{label}: {100 * count // total}%", end="", flush=True)
        if label:
            print()
    finally:
        device.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="action", required=True)
    gif = sub.add_parser("gif")
    gif.add_argument("file", nargs="+")
    gif.add_argument("--where", choices=GIF_TARGETS, default="page")
    gif.add_argument("--fps", type=int)
    gif.add_argument("--fit", choices=("cover", "contain"), default="cover")
    gif.add_argument("--no-theme", action="store_true")
    picture = sub.add_parser("picture")
    picture.add_argument("file")
    picture.add_argument("--fit", choices=("cover", "contain"), default="cover")
    sub.add_parser("view").add_argument("page", choices=("home", "gif", "picture"))
    sub.add_parser("clock")
    sub.add_parser("clear").add_argument("what", choices=("gif", "pictures"))
    args = parser.parse_args()

    if args.action == "gif":
        mode, height, limit = GIF_TARGETS[args.where]
        if len(args.file) > THEME_MAX:
            sys.exit(f"at most {THEME_MAX} GIFs in one medley")
        clips = [load_frames(path, height, args.fit, limit) for path in args.file]
        fps = args.fps or (clips[0][1] if len(clips) == 1 else 10)
        if len(clips) == 1:
            scenes = [clips[0][0]]
        else:
            share = limit // len(clips)  # every scene gets the same number of frames
            scenes = [fill_scene(frames, share) for frames, _ in clips]
        theme = [(len(scene) / fps, *dominant_hue_sat(scene)) for scene in scenes]
        frames = [rgb565_bytes(raw) for scene in scenes for raw in scene]
        print(f"{len(frames)} frames at {fps} fps, {PANEL_W}x{height}, {len(scenes)} scene(s) of {len(scenes[0]) / fps:.1f} s")
        send(gif_stream(frames, mode, fps), "uploading")
        if args.where == "page":
            if not args.no_theme:
                send([(theme_report(theme), 0.05)])
                print("backlight theme: " + ", ".join(f"hue {h} sat {s}" for _, h, s in theme))
            send(simple_stream(PK_GO_GIF))
    elif args.action == "picture":
        frames, _ = load_frames(args.file, 160, args.fit, 1)
        send(picture_stream(rgb565_bytes(frames[0])), "uploading")
    elif args.action == "view":
        send(simple_stream({"home": PK_GO_HOME, "gif": PK_GO_GIF, "picture": PK_NEXT_PIC}[args.page]))
    elif args.action == "clock":
        send(clock_stream(datetime.datetime.now()))
    elif args.action == "clear":
        if args.what == "pictures":
            send(simple_stream(PK_DEL_PIC) * 16)  # one per picture slot
        else:
            send(simple_stream(PK_GIF_NUM, [1]) + simple_stream(PK_GIF_FRAME, [2]))


if __name__ == "__main__":
    main()
