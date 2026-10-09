#!/usr/bin/env python3
"""Drive the YUNZII AL80 screen from the command line.

    al80_screen.py gif <file> [--where page|home|boot] [--fps N] [--fit cover|contain]
    al80_screen.py picture <file> [--fit cover|contain]
    al80_screen.py view home|gif|picture
    al80_screen.py clock
    al80_screen.py clear gif|pictures

Needs the keyboard on USB, and the hidapi and Pillow packages. Works with the factory
USB identity and with the Apple identity our firmware presents in Mac mode.
"""
import argparse
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
        frames.append(rgb565_be(fitted))
    if len(frames) > limit:
        # Keep the whole animation by dropping frames evenly.
        keep = [round(i * (len(frames) - 1) / (limit - 1)) for i in range(limit)]
        scale = len(frames) / limit
        frames = [frames[i] for i in keep]
        durations = [d * scale for d in durations[:limit]]
    mean_ms = sum(durations) / len(durations) if durations and sum(durations) else 100
    return frames, max(1, min(60, round(1000 / mean_ms)))


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
            reply = device.read(64, 500)  # each report is echoed back with an ACK byte
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
    gif.add_argument("file")
    gif.add_argument("--where", choices=GIF_TARGETS, default="page")
    gif.add_argument("--fps", type=int)
    gif.add_argument("--fit", choices=("cover", "contain"), default="cover")
    picture = sub.add_parser("picture")
    picture.add_argument("file")
    picture.add_argument("--fit", choices=("cover", "contain"), default="cover")
    sub.add_parser("view").add_argument("page", choices=("home", "gif", "picture"))
    sub.add_parser("clock")
    sub.add_parser("clear").add_argument("what", choices=("gif", "pictures"))
    args = parser.parse_args()

    if args.action == "gif":
        mode, height, limit = GIF_TARGETS[args.where]
        frames, fps = load_frames(args.file, height, args.fit, limit)
        fps = args.fps or fps
        print(f"{len(frames)} frames at {fps} fps, {PANEL_W}x{height}")
        send(gif_stream(frames, mode, fps), "uploading")
        if args.where == "page":
            send(simple_stream(PK_GO_GIF))
    elif args.action == "picture":
        frames, _ = load_frames(args.file, 160, args.fit, 1)
        send(picture_stream(frames[0]), "uploading")
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
