#!/usr/bin/env python3
"""Unit tests for tools/mrp.py; vectors are shared with firmware/tests/test_mrp.c."""
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mrp  # noqa: E402


class TestMrp(unittest.TestCase):
    def test_crc_check_value(self):
        self.assertEqual(mrp.crc16(b"123456789"), 0x29B1)

    def test_known_frame(self):
        f = mrp.encode(mrp.PTT, 1, mrp.pack_ptt(1))
        self.assertEqual(f[:6], bytes([0xA5, 0x03, 0x01, 0x03, 0x00, 0x01]))
        self.assertEqual(len(f), 6 + 3 + 2)
        self.assertEqual(f.hex(), VECTOR_PTT)

    def test_roundtrip_and_resync(self):
        d = mrp.Decoder()
        good = mrp.encode(mrp.PTT, 1, mrp.pack_ptt(1))
        bad = bytearray(good); bad[7] ^= 0x55
        big = bytes((i * 7) & 0xFF for i in range(640))
        audio = mrp.encode(mrp.AUDIO_TX, 9, big)
        stream = bytes([0x00, 0xA5, 0xFF, 0x12, 0xA5]) + bytes(bad) + good
        frames = d.feed(stream)
        self.assertEqual(frames, [(mrp.PTT, 1, b"\x01\x00\x00")])
        self.assertEqual(d.crc_errors, 1)
        self.assertEqual(d.feed(audio[:100]), [])
        frames = d.feed(audio[100:])
        self.assertEqual(frames, [(mrp.AUDIO_TX, 9, big)])
        # oversize length field with a valid HCS must be dropped
        hdr = bytes([0x10, 0x00, 0xFF, 0xFF]); hcs = hdr[0] ^ hdr[1] ^ hdr[2] ^ hdr[3]
        self.assertEqual(d.feed(bytes([0xA5]) + hdr + bytes([hcs]) + good), [(mrp.PTT, 1, b"\x01\x00\x00")])

    def test_status_layout(self):
        p = struct.pack("<HbBHhBbHhBHHH", 0x11, -101, 16, 7612, -850, 77, -5, 9000, 1500, 1, 9000, 2000, 179)
        self.assertEqual(len(p), 21)
        s = mrp.unpack_status(p)
        self.assertEqual(s["rssi_dbm"], -101)
        self.assertEqual(s["batt_ma"], -850)
        self.assertEqual(s["flags"], ["PTT", "SOURCING"])
        self.assertEqual(s["tx_remaining_s"], 179)

    def test_info_layout(self):
        p = struct.pack("<4BI16s2B", 1, 0, 1, 1, 0xDEADBEEF, b"MR-0001", 0, 16)
        self.assertEqual(len(p), 26)
        self.assertEqual(mrp.unpack_info(p)["serial"], "MR-0001")


VECTOR_PTT = None  # filled below from the C test expectation


def _load_vector():
    """Read the expected bytes out of firmware/tests/test_mrp.c so both sides must agree."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "firmware", "tests", "test_mrp.c")
    with open(path) as fh:
        for line in fh:
            if "expect[] =" in line:
                inner = line.split("{")[1].split("}")[0]
                return "".join(f"{int(x, 16):02x}" for x in inner.replace(" ", "").split(",") if x)
    raise RuntimeError("vector not found in test_mrp.c")


if __name__ == "__main__":
    VECTOR_PTT = _load_vector()
    unittest.main()
