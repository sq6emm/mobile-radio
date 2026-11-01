#!/usr/bin/env python3
"""Unit tests for tools/mrp.py and tools/afsk_wav.py.

The Python implementations are compared byte for byte with the C firmware:
firmware/build/vectors (built by `make -C firmware test`) prints reference
frames, AX.25 bytes, HDLC levels and AFSK samples; this test regenerates them
in Python and asserts equality. If the binary is missing those checks are
skipped with a notice.
"""
import os
import struct
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mrp  # noqa: E402
import afsk_wav  # noqa: E402

VECTORS_BIN = os.path.join(HERE, "..", "firmware", "build", "vectors")


def c_vectors():
    if not os.path.exists(VECTORS_BIN):
        return None
    out = subprocess.run([VECTORS_BIN], capture_output=True, text=True, check=True).stdout
    return dict(line.split(" ", 1) for line in out.strip().splitlines())


class TestMrp(unittest.TestCase):
    def test_crc_check_value(self):
        self.assertEqual(mrp.crc16(b"123456789"), 0x29B1)

    def test_known_frame(self):
        f = mrp.encode(mrp.PTT, 1, mrp.pack_ptt(1))
        self.assertEqual(f.hex(), "a503010300010100004ece")

    def test_roundtrip_and_resync(self):
        d = mrp.Decoder()
        good = mrp.encode(mrp.PTT, 1, mrp.pack_ptt(1))
        bad = bytearray(good); bad[7] ^= 0x55
        big = bytes((i * 7) & 0xFF for i in range(640))
        audio = mrp.encode(mrp.AUDIO_TX, 9, big)
        stream = bytes([0x00, 0xA5, 0xFF, 0x12, 0xA5]) + bytes(bad) + good
        self.assertEqual(d.feed(stream), [(mrp.PTT, 1, b"\x01\x00\x00")])
        self.assertEqual(d.crc_errors, 1)
        self.assertEqual(d.feed(audio[:100]), [])
        self.assertEqual(d.feed(audio[100:]), [(mrp.AUDIO_TX, 9, big)])
        hdr = bytes([0x10, 0x00, 0xFF, 0xFF]); hcs = hdr[0] ^ hdr[1] ^ hdr[2] ^ hdr[3]
        self.assertEqual(d.feed(bytes([0xA5]) + hdr + bytes([hcs]) + good), [(mrp.PTT, 1, b"\x01\x00\x00")])

    def test_status_layout(self):
        p = struct.pack(mrp.STATUS_FMT, 0x11, -101, 16, 7612, -850, 77, -5, 9000, 1500, 1, 9000, 2000, 179, 1, 0, 3, 5, 433500000)
        self.assertEqual(len(p), 29)
        s = mrp.unpack_status(p)
        self.assertEqual(s["rssi_dbm"], -101)
        self.assertEqual(s["flags"], ["PTT", "SOURCING"])
        self.assertEqual(s["mode"], "HAM")
        self.assertEqual(s["freq_hz"], 433500000)
        self.assertTrue(s["dv"]["sync"])

    def test_payload_sizes(self):
        self.assertEqual(len(mrp.pack_set_freq(433500000)), 9)
        self.assertEqual(len(mrp.pack_set_identity("SQ6EMM", 7, "SKI-7")), 17)
        self.assertEqual(len(mrp.pack_set_beacon(True)), 55)
        self.assertEqual(len(mrp.pack_position(51.1, 17.0)), 15)
        self.assertEqual(len(mrp.pack_lora_config(0, 1)), 13)
        self.assertEqual(len(mrp.pack_send_msg(1, "SP6ABC-9", "see you at 18")), 11 + 13)
        with self.assertRaises(ValueError):
            mrp.pack_send_msg(0, "X", "")
        r = mrp.unpack_msg_rx(bytes([0]) + b"SP6ABC\0\0\0\0" + b"12\0\0\0\0" + b"hi")
        self.assertEqual(r, {"transport": 0, "from": "SP6ABC", "id": "12", "text": "hi"})
        a = mrp.unpack_aprs_rx(struct.pack("<B10sBiihH", 0, b"SQ6EMM-7", 1, 511083333, 170375000, -1, 0) + b"hello")
        self.assertEqual(a["src"], "SQ6EMM-7"); self.assertEqual(a["kind"], "position"); self.assertAlmostEqual(a["lat"], 51.1083333)

    def test_ax25_fcs_and_frame(self):
        self.assertEqual(afsk_wav.fcs(b"123456789"), 0x906E)
        f = afsk_wav.build_ui("APZMRP", "SQ6EMM-7", ["WIDE1-1"], b"!5106.50N/01702.25E[mobile-radio")
        self.assertEqual(f[0], ord("A") << 1)
        self.assertEqual(f[6], 0xE0)
        self.assertEqual(f[13], 0x60 | (7 << 1))
        self.assertEqual(f[20], 0x60 | (1 << 1) | 1)
        self.assertEqual(f[21:23], b"\x03\xf0")
        self.assertEqual(afsk_wav.fcs(f[:-2]), struct.unpack("<H", f[-2:])[0])

    def test_against_c_vectors(self):
        v = c_vectors()
        if v is None:
            print("\n(firmware/build/vectors not built: skipping C cross-check)")
            return
        self.assertEqual(mrp.encode(mrp.PTT, 1, mrp.pack_ptt(1)).hex(), v["PTT_KEY_SEQ1"])
        self.assertEqual(mrp.encode(mrp.SET_FREQ, 2, mrp.pack_set_freq(433500000, 4, 1, 0, 12)).hex(), v["SET_FREQ_SEQ2"])
        st = struct.pack(mrp.STATUS_FMT, 0x11, -101, 16, 7612, -850, 77, -5, 9000, 1500, 1, 9000, 2000, 179, 1, 0, 3, 5, 433500000)
        self.assertEqual(mrp.encode(mrp.STATUS, 3, st).hex(), v["STATUS_SEQ3"])
        self.assertEqual(mrp.encode(mrp.SEND_MSG, 4, mrp.pack_send_msg(1, "SP6ABC-9", "see you at 18")).hex(), v["SEND_MSG_SEQ4"])
        frame = afsk_wav.build_ui("APZMRP", "SQ6EMM-7", ["WIDE1-1"], b"!5106.50N/01702.25E[mobile-radio")
        self.assertEqual(frame.hex(), v["AX25_UI"])
        levels = afsk_wav.hdlc_levels(frame, 2, 1)
        self.assertEqual("".join(str(b) for b in levels), v["HDLC_LEVELS"])
        pcm = afsk_wav.afsk(levels, 48000)
        self.assertEqual(len(pcm), int(v["AFSK48K_SAMPLES"]))
        self.assertEqual(" ".join(str(x) for x in pcm[:16]), v["AFSK48K_FIRST16"].strip())


if __name__ == "__main__":
    unittest.main()
