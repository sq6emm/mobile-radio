#!/usr/bin/env python3
"""AX.25 / HDLC / Bell-202 AFSK encoder in Python, mirroring the C modules.

  python3 tools/afsk_wav.py --src SQ6EMM-7 --dst APZMRP --path WIDE1-1 \
      --info '!5106.50N/01702.25E[mobile-radio' --out packet.wav

The WAV (48 kHz mono s16) can be played into Direwolf or a hardware TNC for an
independent check of the firmware's framing and modulation, and the module
is imported by tools/test_mrp.py to compare bytes with firmware/build/vectors.
"""
import argparse
import math
import struct
import wave

BAUD, MARK, SPACE = 1200, 1200, 2200


def fcs(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8408 if crc & 1 else crc >> 1
    return (~crc) & 0xFFFF


def addr(call_ssid: str, last: bool, cmd_bit: bool = False) -> bytes:
    call, _, ssid = call_ssid.upper().partition("-")
    ssid = int(ssid) if ssid else 0
    if not 1 <= len(call) <= 6 or not 0 <= ssid <= 15:
        raise ValueError("bad address " + call_ssid)
    out = bytes((ord(c) << 1) for c in call.ljust(6))
    return out + bytes([0x60 | (ssid << 1) | (1 if last else 0) | (0x80 if cmd_bit else 0)])


def build_ui(dst: str, src: str, path: list, info: bytes) -> bytes:
    frame = addr(dst, False, cmd_bit=True) + addr(src, not path)
    for i, d in enumerate(path):
        frame += addr(d, i == len(path) - 1)
    frame += bytes([0x03, 0xF0]) + info
    return frame + struct.pack("<H", fcs(frame))


def hdlc_levels(frame: bytes, preamble_flags=20, tail_flags=3, level=0) -> list:
    """NRZI line levels (0/1 per bit): flags, stuffed frame, flags."""
    out = []

    def emit(bit):
        nonlocal level
        if not bit:
            level ^= 1
        out.append(level)

    def flag():
        for b in range(8):
            emit((0x7E >> b) & 1)

    for _ in range(preamble_flags):
        flag()
    ones = 0
    for byte in frame:
        for b in range(8):
            bit = (byte >> b) & 1
            emit(bit)
            if bit:
                ones += 1
                if ones == 5:
                    emit(0)
                    ones = 0
            else:
                ones = 0
    for _ in range(tail_flags):
        flag()
    return out


def afsk(levels, fs=48000, amplitude=8000):
    """Continuous-phase Bell 202 modulation; identical arithmetic to afsk.c."""
    table = [int(round(32767.0 * math.sin(2 * math.pi * i / 1024))) for i in range(1024)]
    mark_inc = int(MARK / fs * 4294967296.0)
    space_inc = int(SPACE / fs * 4294967296.0)
    phase, frac, pcm = 0, 0, []
    for lv in levels:
        frac += fs
        ns = frac // BAUD
        frac -= ns * BAUD
        inc = mark_inc if lv else space_inc
        for _ in range(ns):
            phase = (phase + inc) & 0xFFFFFFFF
            pcm.append((table[phase >> 22] * amplitude) >> 15)
    return pcm


def write_wav(path, pcm, fs=48000):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(fs)
        w.writeframes(struct.pack("<%dh" % len(pcm), *pcm))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--src", required=True)
    ap.add_argument("--dst", default="APZMRP")
    ap.add_argument("--path", default="WIDE1-1", help="comma separated, '' for none")
    ap.add_argument("--info", required=True)
    ap.add_argument("--out", default="packet.wav")
    ap.add_argument("--fs", type=int, default=48000)
    ap.add_argument("--txdelay", type=int, default=20, help="preamble flags (20 = ~130 ms)")
    a = ap.parse_args()
    path = [p for p in a.path.split(",") if p]
    frame = build_ui(a.dst, a.src, path, a.info.encode())
    levels = hdlc_levels(frame, a.txdelay, 3)
    pcm = afsk(levels, a.fs)
    # 0.5 s of silence either side so decoders settle
    pad = [0] * (a.fs // 2)
    write_wav(a.out, pad + pcm + pad, a.fs)
    print(f"frame {len(frame)} bytes, fcs 0x{fcs(frame[:-2]):04x}, {len(levels)} bits, {len(pcm)} samples -> {a.out}")


if __name__ == "__main__":
    main()
