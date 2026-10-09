"""Device-free tests for al80_screen.py, checked against bytes captured from the vendor app.

    run:  python -m unittest tooling/test_al80_screen.py
"""
import datetime
import json
import pathlib
import unittest

import al80_screen as s

CAPTURE = pathlib.Path(__file__).parent.parent / "research/gif_capture/testgif_capture_raw.json"


def hexrow(packet):
    return " ".join(f"{b:02x}" for b in packet)


class Packets(unittest.TestCase):
    def test_crc_matches_known_commands(self):
        self.assertEqual(s.crc16_modbus([0x0B, 0, 0]), 0x0200)  # home
        self.assertEqual(s.crc16_modbus([0x0D, 0, 0]), 0x03E0)  # picture
        self.assertEqual(s.crc16_modbus([0x0F, 0, 0]), 0xC341)  # gif
        self.assertEqual(s.crc16_modbus([0x09, 0, 3]), 0xC3E1)  # time

    def test_finish_is_the_vendor_constant(self):
        self.assertEqual(s.FINISH[:5], [0x42, 0x00, 0x00, 0x38, 0x7A])
        self.assertEqual(len(s.FINISH), 64)

    def test_clock_matches_documented_bytes(self):
        steps = s.clock_stream(datetime.datetime(2026, 7, 1, 6, 23, 9))
        self.assertEqual(steps[0][0][:14], [0x40, 0, 0, 7, 0xF6, 0x02, 0, 0xA5, 0x5A, 0x09, 0, 3, 0xC3, 0xE1])
        self.assertEqual(steps[1][0][:10], [0x41, 0, 0, 3, 0x6A, 0, 0, 6, 23, 9])
        self.assertEqual(steps[4][0][7:11], [26, 3, 7, 1])  # year, weekday, month, day
        self.assertEqual(len(steps), 18)

    def test_still_picture_matches_documented_sequence(self):
        steps = s.picture_stream(bytes(30720))
        self.assertEqual(hexrow(steps[0][0][:15]), "40 00 00 08 cf 02 00 a5 5a 10 00 01 c5 b1 01")
        self.assertEqual(hexrow(steps[1][0][:14]), "41 00 00 07 21 03 00 a5 5a 0c 78 00 c3 93")
        self.assertEqual(len(steps), 2 + 549 + 1)       # 548 full blocks and a 32-byte tail
        self.assertEqual(steps[-2][0][1:4], [0xE0, 0x77, 32])
        self.assertEqual(steps[0][1], 0.3)              # the module needs this settle to commit

    def test_data_blocks_carry_offset_and_checksum(self):
        blocks = s.data_reports(bytes(1024))
        self.assertEqual(len(blocks), 19)               # 18 full blocks and a 16-byte tail
        self.assertEqual([b[4] for b in blocks[:3]], [121, 177, 233])
        self.assertEqual(blocks[-1][1:4], [0xF0, 0x03, 16])

    def test_gif_offsets_restart_in_every_bank(self):
        steps = s.gif_stream([bytes(30720)], mode=1, fps=30)
        data = [p for p, _ in steps if p[0] == 0x41 and p[7:9] != [0xA5, 0x5A]]
        self.assertEqual(len(data), 30 * 19)
        self.assertEqual(max(p[1] | (p[2] << 8) for p in data), 0x03F0)

    def test_gif_ends_with_count_then_fps(self):
        steps = s.gif_stream([bytes(12288)] * 3, mode=2, fps=12)
        self.assertEqual(steps[-3][0][7:16], [0xA5, 0x5A, 0x12, 0, 2, 0x04, 0x50, 2, 3])
        self.assertEqual(steps[-2][0][7:16], [0xA5, 0x5A, 0x13, 0, 2, 0xC4, 0x01, 2, 12])
        self.assertEqual(steps[-1][0], s.FINISH)

    def test_gif_pauses_for_the_flash_write(self):
        steps = s.gif_stream([bytes(12288)] * 17, mode=2, fps=10)
        headers = [pause for p, pause in steps if p[7:10] == [0xA5, 0x5A, 0x10]]
        self.assertEqual([h for h in headers if h == 3.0], [3.0, 3.0])  # frames 0 and 16

    @unittest.skipUnless(CAPTURE.exists(), "vendor capture not present")
    def test_gif_control_packets_match_vendor_capture(self):
        captured = [bytes.fromhex(r["hex"].replace(" ", "")) for r in json.loads(CAPTURE.read_text())]
        wanted = {bytes(p) for p, _ in s.gif_stream([bytes(30720)] * 3, mode=1, fps=30)[:2]}
        wanted |= {bytes(p) for p, _ in s.gif_stream([bytes(30720)] * 3, mode=1, fps=30) if p[7:10] in ([0xA5, 0x5A, 0x10], [0xA5, 0x5A, 0x11])}
        seen = {c[:64].ljust(64, b"\0") for c in captured}
        self.assertEqual(len(wanted), 6)                # announce, setup, three headers, one length
        self.assertTrue(wanted <= seen, [hexrow(w[:18]) for w in wanted - seen])

    def test_rgb565_is_big_endian(self):
        from PIL import Image
        self.assertEqual(s.rgb565_be(Image.new("RGB", (1, 1), (255, 0, 0))), b"\xF8\x00")
        self.assertEqual(s.rgb565_be(Image.new("RGB", (1, 1), (0, 0, 255))), b"\x00\x1F")


if __name__ == "__main__":
    unittest.main()
