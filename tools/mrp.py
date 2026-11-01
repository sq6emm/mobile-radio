#!/usr/bin/env python3
"""MRP (Mobile Radio Protocol) reference implementation and bench CLI.

Pure-Python framing that mirrors firmware/src/mrp.c byte for byte, plus a
small CLI that talks to the pack over USB (pyusb) or a serial port
(pyserial) for bench testing without a phone.

    python3 tools/mrp.py vectors                  # print shared test vectors
    python3 tools/mrp.py --serial /dev/ttyUSB0 info
    python3 tools/mrp.py --usb channel 8 --ctcss 12 --squelch 4 --power high
    python3 tools/mrp.py --usb ptt 3              # key for 3 s with keep-alives
    python3 tools/mrp.py --usb status --follow

Frame:  A5 | type | seq | len16 LE | hcs | payload | crc16 LE
hcs  = type ^ seq ^ len_lo ^ len_hi
crc  = CRC-16/CCITT-FALSE over type..payload
"""
import argparse
import struct
import sys
import time

SOF = 0xA5
HDR_LEN = 6
CRC_LEN = 2
MAX_PAYLOAD = 1024

# message types
GET_INFO, SET_CHANNEL, PTT, SET_AUDIO, SET_POWER, DATA_TX, GET_STATUS, KEEPALIVE = range(1, 9)
SET_MODE, SET_FREQ, SET_IDENTITY, SET_BEACON, POSITION, SEND_MSG, SET_PROFILE = range(9, 16)
AUDIO_TX, LORA_CONFIG, LORA_TX, SET_DV = 0x10, 0x11, 0x12, 0x13
NAK, INFO, ACK, AUDIO_RX, STATUS, DATA_RX = 0x7F, 0x81, 0x82, 0x90, 0x91, 0x92
MSG_RX, MSG_STATUS, LORA_RX, APRS_RX = 0x93, 0x94, 0x95, 0x96
TR_FM, TR_LORA433, TR_LORA868 = 0, 1, 2
MSG_STATE = {0: "free", 1: "pending", 2: "sent_wait_ack", 3: "acked", 4: "failed"}
APRS_KIND = {0: "unknown", 1: "position", 2: "message", 3: "ack", 4: "rej", 5: "status"}
TYPE_NAMES = {v: k for k, v in globals().items() if k.isupper() and isinstance(v, int) and k not in
              ("SOF", "HDR_LEN", "CRC_LEN", "MAX_PAYLOAD")}

USB_VID, USB_PID = 0x1209, 0x0001       # pid.codes test PID - replace before release
USB_IFACE_VENDOR = 0

RESULT_NAMES = {0: "OK", 1: "BAD_LEN", 2: "BAD_VALUE", 3: "RADIO", 4: "BUSY", 5: "INHIBITED", 6: "UNSUPPORTED"}


def crc16(data: bytes, crc: int = 0xFFFF) -> int:
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def encode(mtype: int, seq: int, payload: bytes = b"") -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload too long")
    hdr = bytes([mtype & 0xFF, seq & 0xFF, len(payload) & 0xFF, len(payload) >> 8])
    hcs = hdr[0] ^ hdr[1] ^ hdr[2] ^ hdr[3]
    body = hdr + bytes([hcs]) + payload
    return bytes([SOF]) + body + struct.pack("<H", crc16(body))


class Decoder:
    """Streaming decoder; feed() returns a list of (type, seq, payload)."""

    def __init__(self):
        self.buf = bytearray()
        self.frames_ok = self.crc_errors = self.resyncs = 0

    def feed(self, data: bytes):
        out = []
        self.buf += data
        while self.buf:
            if self.buf[0] != SOF:
                i = self.buf.find(bytes([SOF]))
                del self.buf[: i if i >= 0 else len(self.buf)]
                continue
            if len(self.buf) < HDR_LEN:
                break
            t, s, lo, hi, hcs = self.buf[1:6]
            n = lo | (hi << 8)
            if (t ^ s ^ lo ^ hi) != hcs or n > MAX_PAYLOAD:
                self.resyncs += 1
                del self.buf[0]
                continue
            total = HDR_LEN + n + CRC_LEN
            if len(self.buf) < total:
                break
            if crc16(bytes(self.buf[1:total - 2])) != struct.unpack_from("<H", self.buf, total - 2)[0]:
                self.crc_errors += 1
                self.resyncs += 1
                del self.buf[0]
                continue
            out.append((t, s, bytes(self.buf[HDR_LEN:total - 2])))
            self.frames_ok += 1
            del self.buf[:total]
        return out


# ---------------------------------------------------------------- payloads
def pack_set_channel(channel, bandwidth=0, ctcss_tx=0, ctcss_rx=0, squelch=4, power=0, flags=0):
    return struct.pack("<7B", channel, bandwidth, ctcss_tx, ctcss_rx, squelch, power, flags)


def pack_ptt(state, ttl_ms=0):
    return struct.pack("<BH", 1 if state else 0, ttl_ms)


def pack_set_audio(volume=4, mic_gain=8, filter_flags=0):
    return struct.pack("<3B", volume, mic_gain, filter_flags)


def pack_set_power(source=True, charge=True, max_source_w=0, reserve_soc=0):
    return struct.pack("<3B", (1 if source else 0) | (2 if charge else 0), max_source_w, reserve_soc)


def _cstr(s, n):
    return s.encode()[:n].ljust(n, b"\0")


def pack_set_mode(ham):
    return bytes([1 if ham else 0])


def pack_set_freq(hz, squelch=4, power=0, ctcss_tx=0, ctcss_rx=0, bandwidth=0):
    return struct.pack("<I5B", hz, squelch, power, ctcss_tx, ctcss_rx, bandwidth)


def pack_set_identity(callsign="", ssid=0, nickname=""):
    return _cstr(callsign, 8) + bytes([ssid]) + _cstr(nickname, 8)


def pack_set_beacon(enabled, smart=True, interval_s=600, sym_table="/", sym_code="[", transport=TR_FM,
                    path="WIDE1-1", comment=""):
    return (struct.pack("<BBH", 1 if enabled else 0, 1 if smart else 0, interval_s) +
            sym_table.encode() + sym_code.encode() + bytes([transport]) + _cstr(path, 16) + _cstr(comment, 32))


def pack_position(lat, lon, alt_m=0, speed_kmh=0, course_deg=-1, valid=True):
    return struct.pack("<iihHhB", int(round(lat * 1e7)), int(round(lon * 1e7)), alt_m, speed_kmh, course_deg, 1 if valid else 0)


def pack_send_msg(transport, to, text):
    t = text.encode()
    if not 1 <= len(t) <= 67:
        raise ValueError("text 1..67 bytes")
    return bytes([transport]) + _cstr(to, 10) + t


def pack_set_profile(soundcard):
    return bytes([1 if soundcard else 0])


def pack_lora_config(radio, preset=0, freq_hz=0, sf=9, bw=4, cr=1, power_dbm=10, sync=0x1424, rx_pref=0):
    return struct.pack("<BBI3BbHB", radio, preset, freq_hz, sf, bw, cr, power_dbm, sync, rx_pref)


def pack_set_dv(on):
    return bytes([1 if on else 0])


def unpack_msg_rx(p):
    tr, frm, mid = p[0], p[1:11].split(b"\0")[0].decode(errors="replace"), p[11:17].split(b"\0")[0].decode()
    return {"transport": tr, "from": frm, "id": mid, "text": p[17:].decode(errors="replace")}


def unpack_msg_status(p):
    mid, st = struct.unpack("<HB", p)
    return {"id": mid, "state": MSG_STATE.get(st, st)}


def unpack_lora_rx(p):
    radio, rssi, snr = struct.unpack_from("<BhB", p)
    return {"radio": 433 if radio == 0 else 868, "rssi_dbm": rssi, "snr_db": struct.unpack("<b", bytes([snr]))[0],
            "payload": p[4:]}


def unpack_aprs_rx(p):
    tr, src, kind, lat, lon, course, speed = struct.unpack_from("<B10sBiihH", p)
    return {"transport": tr, "src": src.split(b"\0")[0].decode(errors="replace"), "kind": APRS_KIND.get(kind, kind),
            "lat": lat / 1e7, "lon": lon / 1e7, "course": course, "speed_kmh": speed,
            "text": p[24:].decode(errors="replace")}


def unpack_ack(p):
    req, res, detail = struct.unpack("<BBH", p)
    return {"req": TYPE_NAMES.get(req, hex(req)), "result": RESULT_NAMES.get(res, res), "detail": detail}


def unpack_info(p):
    proto, maj, mn, hw, caps, serial, region, nch = struct.unpack("<4BI16s2B", p)
    return {"proto": proto, "fw": f"{maj}.{mn}", "hw_rev": hw, "caps": hex(caps),
            "serial": serial.split(b"\0")[0].decode(errors="replace"), "region": region, "channels": nch}


STATUS_FLAGS = ["PTT", "SQ_OPEN", "TX_INHIBIT", "CHARGING", "SOURCING", "FAULT", "RADIO_OK", "HOST_ALIVE"]


STATUS_FMT = "<HbBHhBbHhBHHH4BI"   # 29 bytes


def unpack_status(p):
    (flags, rssi, ch, batt_mv, batt_ma, soc, temp, vbus_mv, vbus_ma, pd_role,
     pd_mv, pd_ma, tx_rem, mode, profile, dv, lora, freq) = struct.unpack(STATUS_FMT, p)
    return {"flags": [n for i, n in enumerate(STATUS_FLAGS) if flags & (1 << i)],
            "rssi_dbm": rssi, "channel": ch, "batt_mv": batt_mv, "batt_ma": batt_ma, "soc_pct": soc,
            "temp_c": temp, "vbus_mv": vbus_mv, "vbus_ma": vbus_ma,
            "pd": {"source": bool(pd_role & 1), "dfp": bool(pd_role & 2), "contract": bool(pd_role & 4),
                   "partner_drp": bool(pd_role & 8), "mv": pd_mv, "ma": pd_ma},
            "tx_remaining_s": tx_rem, "mode": "HAM" if mode else "PMR446",
            "profile": "soundcard" if profile else "app",
            "dv": {"enabled": bool(dv & 1), "sync": bool(dv & 2)},
            "lora": {"433_listen": bool(lora & 1), "433_tx": bool(lora & 2), "868_listen": bool(lora & 4), "868_tx": bool(lora & 8)},
            "freq_hz": freq}


# --------------------------------------------------------------- transports
class SerialLink:
    def __init__(self, port, baud=115200):
        import serial  # pyserial
        self.s = serial.Serial(port, baud, timeout=0.05)

    def write(self, b): self.s.write(b)
    def read(self): return self.s.read(4096)


class UsbLink:
    def __init__(self, vid=USB_VID, pid=USB_PID):
        import usb.core, usb.util  # pyusb
        self.dev = usb.core.find(idVendor=vid, idProduct=pid)
        if self.dev is None:
            raise SystemExit("device not found")
        cfg = self.dev.get_active_configuration()
        intf = cfg[(USB_IFACE_VENDOR, 0)]
        if self.dev.is_kernel_driver_active(intf.bInterfaceNumber):
            self.dev.detach_kernel_driver(intf.bInterfaceNumber)
        usb.util.claim_interface(self.dev, intf.bInterfaceNumber)
        self.ep_out = usb.util.find_descriptor(intf, custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_OUT)
        self.ep_in = usb.util.find_descriptor(intf, custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_IN)

    def write(self, b): self.ep_out.write(b)

    def read(self):
        import usb.core
        try:
            return bytes(self.ep_in.read(4096, timeout=50))
        except usb.core.USBTimeoutError:
            return b""


class Session:
    def __init__(self, link):
        self.link, self.dec, self.seq = link, Decoder(), 0

    def send(self, t, payload=b""):
        self.link.write(encode(t, self.seq, payload))
        self.seq = (self.seq + 1) & 0xFF

    def poll(self, timeout=1.0, want=None):
        end = time.time() + timeout
        while time.time() < end:
            for t, s, p in self.dec.feed(self.link.read()):
                if want is None or t == want:
                    return t, s, p
        return None


def show(t, p):
    if t == ACK: print("ACK", unpack_ack(p))
    elif t == INFO: print("INFO", unpack_info(p))
    elif t == STATUS: print("STATUS", unpack_status(p))
    elif t == AUDIO_RX: print(f"AUDIO_RX {len(p)} bytes")
    elif t == MSG_RX: print("MSG_RX", unpack_msg_rx(p))
    elif t == MSG_STATUS: print("MSG_STATUS", unpack_msg_status(p))
    elif t == LORA_RX: print("LORA_RX", unpack_lora_rx(p))
    elif t == APRS_RX: print("APRS_RX", unpack_aprs_rx(p))
    else: print(TYPE_NAMES.get(t, hex(t)), p.hex())


def cmd_vectors(_):
    print("crc16('123456789') =", hex(crc16(b"123456789")))
    print("PTT key seq1        =", encode(PTT, 1, pack_ptt(1)).hex(" "))
    print("GET_INFO seq0       =", encode(GET_INFO, 0).hex(" "))
    print("SET_CHANNEL ch7 seq2=", encode(SET_CHANNEL, 2, pack_set_channel(7, 0, 12, 12, 4, 1, 1)).hex(" "))
    print("SET_FREQ seq2       =", encode(SET_FREQ, 2, pack_set_freq(433500000, 4, 1, 0, 12)).hex(" "))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--serial", help="serial port (bench UART bridge)")
    ap.add_argument("--usb", action="store_true", help="talk over the vendor USB interface")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("vectors").set_defaults(fn=cmd_vectors)
    sub.add_parser("info")
    p = sub.add_parser("channel"); p.add_argument("channel", type=int)
    p.add_argument("--ctcss", type=int, default=0); p.add_argument("--squelch", type=int, default=4)
    p.add_argument("--power", choices=["low", "high"], default="low"); p.add_argument("--bclo", action="store_true")
    p = sub.add_parser("ptt"); p.add_argument("seconds", type=float)
    p = sub.add_parser("audio"); p.add_argument("--volume", type=int, default=4); p.add_argument("--mic", type=int, default=8)
    p = sub.add_parser("power"); p.add_argument("--no-source", action="store_true"); p.add_argument("--no-charge", action="store_true")
    p.add_argument("--reserve", type=int, default=0)
    p = sub.add_parser("status"); p.add_argument("--follow", action="store_true")
    p = sub.add_parser("mode"); p.add_argument("mode", choices=["pmr", "ham"])
    p = sub.add_parser("freq"); p.add_argument("mhz", type=float); p.add_argument("--squelch", type=int, default=4)
    p.add_argument("--power", choices=["low", "high"], default="low"); p.add_argument("--ctcss", type=int, default=0)
    p = sub.add_parser("identity"); p.add_argument("--callsign", default=""); p.add_argument("--ssid", type=int, default=0)
    p.add_argument("--nick", default="")
    p = sub.add_parser("beacon"); p.add_argument("state", choices=["on", "off"]); p.add_argument("--interval", type=int, default=600)
    p.add_argument("--fixed", action="store_true"); p.add_argument("--transport", type=int, default=TR_FM)
    p.add_argument("--symbol", default="/["); p.add_argument("--path", default="WIDE1-1"); p.add_argument("--comment", default="")
    p = sub.add_parser("pos"); p.add_argument("lat", type=float); p.add_argument("lon", type=float)
    p.add_argument("--alt", type=int, default=0); p.add_argument("--speed", type=int, default=0); p.add_argument("--course", type=int, default=-1)
    p = sub.add_parser("msg"); p.add_argument("to"); p.add_argument("text"); p.add_argument("--transport", type=int, default=TR_FM)
    p = sub.add_parser("profile"); p.add_argument("profile", choices=["app", "soundcard"])
    p = sub.add_parser("lora"); p.add_argument("radio", choices=["433", "868"])
    p.add_argument("--preset", type=int, default=0, help="0 custom 1 aprs433 2 meshtastic868 3 pack433 4 pack868")
    p.add_argument("--mhz", type=float, default=0); p.add_argument("--sf", type=int, default=9); p.add_argument("--bw", type=int, default=4)
    p.add_argument("--cr", type=int, default=1); p.add_argument("--power", type=int, default=10); p.add_argument("--sync", type=lambda x: int(x, 0), default=0x1424)
    p.add_argument("--listen", action="store_true", help="prefer this LoRa receiver over FM when idle (433 only)")
    p = sub.add_parser("dv"); p.add_argument("state", choices=["on", "off"])
    p = sub.add_parser("listen", help="print every incoming message / position / LoRa frame")
    a = ap.parse_args()
    if a.cmd == "vectors":
        return cmd_vectors(a)

    link = SerialLink(a.serial) if a.serial else UsbLink()
    s = Session(link)
    if a.cmd == "info":
        s.send(GET_INFO); r = s.poll(want=INFO); show(*r[::2]) if r else print("no reply")
    elif a.cmd == "channel":
        s.send(SET_CHANNEL, pack_set_channel(a.channel, 0, a.ctcss, a.ctcss, a.squelch,
                                             1 if a.power == "high" else 0, 1 if a.bclo else 0))
        r = s.poll(want=ACK); show(*r[::2]) if r else print("no reply")
    elif a.cmd == "ptt":
        s.send(PTT, pack_ptt(1, 2000)); r = s.poll(want=ACK); show(*r[::2]) if r else print("no reply")
        end = time.time() + a.seconds
        while time.time() < end:
            s.send(KEEPALIVE); time.sleep(0.5)
        s.send(PTT, pack_ptt(0)); r = s.poll(want=ACK); show(*r[::2]) if r else print("no reply")
    elif a.cmd == "audio":
        s.send(SET_AUDIO, pack_set_audio(a.volume, a.mic)); r = s.poll(want=ACK); show(*r[::2]) if r else print("no reply")
    elif a.cmd == "power":
        s.send(SET_POWER, pack_set_power(not a.no_source, not a.no_charge, 0, a.reserve))
        r = s.poll(want=ACK); show(*r[::2]) if r else print("no reply")
    elif a.cmd == "status":
        while True:
            r = s.poll(timeout=2.0, want=STATUS)
            if r: show(r[0], r[2])
            else: print("no status in 2 s")
            if not a.follow: break
    elif a.cmd == "mode":
        s.send(SET_MODE, pack_set_mode(a.mode == "ham")); r = s.poll(want=ACK); show(*r[::2]) if r else print("no reply")
    elif a.cmd == "freq":
        s.send(SET_FREQ, pack_set_freq(int(round(a.mhz * 1e6)), a.squelch, 1 if a.power == "high" else 0, a.ctcss, a.ctcss))
        r = s.poll(want=ACK); show(*r[::2]) if r else print("no reply")
    elif a.cmd == "identity":
        s.send(SET_IDENTITY, pack_set_identity(a.callsign.upper(), a.ssid, a.nick)); r = s.poll(want=ACK); show(*r[::2]) if r else print("no reply")
    elif a.cmd == "beacon":
        s.send(SET_BEACON, pack_set_beacon(a.state == "on", not a.fixed, a.interval, a.symbol[0], a.symbol[1], a.transport, a.path, a.comment))
        r = s.poll(want=ACK); show(*r[::2]) if r else print("no reply")
    elif a.cmd == "pos":
        s.send(POSITION, pack_position(a.lat, a.lon, a.alt, a.speed, a.course))
    elif a.cmd == "msg":
        s.send(SEND_MSG, pack_send_msg(a.transport, a.to.upper(), a.text)); r = s.poll(want=ACK); show(*r[::2]) if r else print("no reply")
    elif a.cmd == "profile":
        s.send(SET_PROFILE, pack_set_profile(a.profile == "soundcard")); r = s.poll(want=ACK); show(*r[::2]) if r else print("no reply (re-enumerating)")
    elif a.cmd == "lora":
        s.send(LORA_CONFIG, pack_lora_config(0 if a.radio == "433" else 1, a.preset, int(round(a.mhz * 1e6)), a.sf, a.bw, a.cr,
                                             a.power, a.sync, 1 if a.listen else 0))
        r = s.poll(want=ACK); show(*r[::2]) if r else print("no reply")
    elif a.cmd == "dv":
        s.send(SET_DV, pack_set_dv(a.state == "on")); r = s.poll(want=ACK); show(*r[::2]) if r else print("no reply")
    elif a.cmd == "listen":
        while True:
            r = s.poll(timeout=5.0)
            if r and r[0] in (MSG_RX, MSG_STATUS, LORA_RX, APRS_RX, DATA_RX): show(r[0], r[2])


if __name__ == "__main__":
    sys.exit(main())
