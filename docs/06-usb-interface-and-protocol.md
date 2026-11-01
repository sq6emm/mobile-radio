# 6. USB interface and the MRP protocol

## 6.1 Enumeration: two profiles

USB 2.0 full speed (12 Mbit/s), bus-powered descriptor (`bMaxPower` 100 mA,
the pack really runs from its own battery) so that no host ever has to supply
more than it would to a mouse. The pack has two USB personalities and
re-enumerates (soft disconnect) when switching; the choice is persisted.

**Profile 0 "app"** (default): for the phone app and bench tools.

| # | Interface | Class | Endpoints | Purpose |
|---|-----------|-------|-----------|---------|
| 0 | MRP | 0xFF vendor, subclass 0x01, protocol 0x01 | EP1 OUT bulk 64 B, EP1 IN bulk 64 B | control, status, PCM audio, messages, LoRa, APRS (this document) |
| 1-2 | Serial | CDC-ACM | EP2 INT, EP3 bulk in/out | debug console / AT pass-through; RTS also keys PTT so Direwolf-style programs work even in this profile |

**Profile 1 "sound-card"** (Digirig-compatible): for iOS and for existing
digital-mode programs (Direwolf, FreeDV GUI, SSTV, VARA FM, JS8Call ...).

| # | Interface | Class | Endpoints | Purpose |
|---|-----------|-------|-----------|---------|
| 0-2 | Audio | UAC 2.0, 48 kHz mono s16 in (receiver) and out (transmitter), asynchronous with feedback | isochronous | the radio's audio, flat path, no processing |
| 3-4 | Serial | CDC-ACM | EP INT, bulk in/out | **RTS = PTT** (DTR optional), same as a Digirig; the console is still available |

Switching: `SET_PROFILE` over MRP, a long press of the mode button, or the
stored setting at power-up. The pack's own PTT button and channel memory keep
working in either profile, so an iPhone user talks with the button and
listens through the "headset".

- VID/PID: development uses pid.codes 0x1209 / 0x0001 (test allocation); a
  real PID must be obtained before any unit leaves the bench. The two
  profiles use different PIDs so operating systems cache the right driver.
- Microsoft OS 2.0 descriptors bind WinUSB to interface 0 automatically.
- BOS also carries a WebUSB platform descriptor so a browser-based bench tool
  can talk to it; not needed by the phone app.
- Strings: manufacturer, product "mobile-radio PMR446 pack", serial = unique
  from the MCU UID (also reported in `INFO`).

Why no USB Audio Class by default: Android treats any UAC device as the
phone's audio output and reroutes ringtones, media and calls to it the moment
it is plugged in. For a PTT accessory that is wrong. Streaming PCM over the
vendor interface keeps the phone's audio untouched; the app plays and records
with `AudioTrack` / `AudioRecord` as it chooses. UAC remains as a profile for
hosts without an app (iOS), where the pack's own PTT button and channel memory
are used.

## 6.2 Frame format

```
offset  size  field
0       1     SOF = 0xA5
1       1     type
2       1     seq        (per-direction counter, wraps; audio uses it for loss detection)
3       2     len        little-endian payload length, 0..1024
5       1     HCS        type ^ seq ^ len_lo ^ len_hi
6       len   payload
6+len   2     CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) over bytes 1..5+len, little-endian
```

Frames are packed back to back in bulk transfers; a transfer may contain
several frames and a frame may span transfers. Receivers resynchronise on
SOF + HCS and verify the CRC. Reference implementations:
`firmware/src/mrp.c` and `tools/mrp.py`. `firmware/build/vectors` (built by
`make -C firmware test`) prints reference frames, an AX.25 frame, its HDLC
bit stream and the first AFSK samples; `tools/test_mrp.py` regenerates all of
them in Python and asserts byte equality.

## 6.3 Messages

Host to device:

| Type | Name | Payload | Reply |
|------|------|---------|-------|
| 0x01 | GET_INFO | - | INFO |
| 0x02 | SET_CHANNEL | channel u8 (1-16), bandwidth u8 (0), ctcss_tx u8 (0-38), ctcss_rx u8, squelch u8 (0-8), power u8 (0 low / 1 high), flags u8 (bit0 busy-channel lockout) | ACK |
| 0x03 | PTT | state u8, ttl_ms u16 (keep-alive window, 0 = 2000) | ACK (result BUSY / INHIBITED when refused) |
| 0x04 | SET_AUDIO | volume u8 (1-8), mic_gain u8 (0-15), filter_flags u8 (bit0 bypass emphasis, bit1 bypass 300 Hz HPF, bit2 bypass 3 kHz LPF) | ACK |
| 0x05 | SET_POWER | flags u8 (bit0 allow sourcing to phone, bit1 allow charging from port), max_source_w u8, reserve_soc_pct u8 | ACK |
| 0x06 | DATA_TX | up to 200 bytes of application data (phase 2, AFSK) | ACK |
| 0x07 | GET_STATUS | - | STATUS |
| 0x08 | KEEPALIVE | - | - |
| 0x09 | SET_MODE | mode u8 (0 PMR446, 1 HAM 70 cm) | ACK (INHIBITED when no callsign stored) |
| 0x0A | SET_FREQ | hz u32, squelch u8, power u8, ctcss_tx u8, ctcss_rx u8, bandwidth u8 (HAM mode only, 430-440 MHz on 12.5 kHz) | ACK |
| 0x0B | SET_IDENTITY | callsign char[8], ssid u8, nickname char[8] (empty = keep); a valid callsign unlocks HAM mode | ACK (detail = unlocked) |
| 0x0C | SET_BEACON | enabled u8, smart u8, interval_s u16, sym_table u8, sym_code u8, transport u8 (0 FM, 1 LoRa433, 2 LoRa868, 3 FM+LoRa433), path char[16], comment char[32] | ACK |
| 0x0D | POSITION | lat_e7 i32, lon_e7 i32, alt_m i16, speed_kmh u16, course_deg i16 (-1 none), valid u8 | - |
| 0x0E | SEND_MSG | transport u8, to char[10], text 1-67 bytes | ACK (detail = message id) |
| 0x0F | SET_PROFILE | profile u8 (0 app, 1 sound-card) | ACK, then re-enumeration |
| 0x10 | AUDIO_TX | 640 bytes s16le PCM, 16 kHz, 20 ms | - |
| 0x11 | LORA_CONFIG | radio u8 (0 = 433, 1 = 868), preset u8 (0 custom, 1 LoRa-APRS 433, 2 Meshtastic EU_868 PHY, 3 pack 433, 4 pack 868), freq_hz u32, sf u8, bw u8 (4 = 125 k, 5 = 250 k, 6 = 500 k), cr u8 (1-4), power_dbm i8, sync u16, rx_pref u8 (433: prefer LoRa receiver over FM when idle) | ACK |
| 0x12 | LORA_TX | radio u8, raw payload 1-255 bytes | ACK |
| 0x13 | SET_DV | enabled u8 (FreeDV 2400B; HAM mode only) | ACK |

Device to host:

| Type | Name | Payload |
|------|------|---------|
| 0x81 | INFO | proto u8, fw_major u8, fw_minor u8, hw_rev u8, caps u32 (bit0 voice, bit1 data, bit2 PD source, bit3 PD sink, bit4 UAC profile), serial char[16], region u8 (0 = EU PMR446), n_channels u8 |
| 0x82 | ACK | req_type u8, result u8 (0 OK, 1 BAD_LEN, 2 BAD_VALUE, 3 RADIO, 4 BUSY, 5 INHIBITED, 6 UNSUPPORTED), detail u16 |
| 0x90 | AUDIO_RX | 640 bytes s16le PCM |
| 0x91 | STATUS | flags u16 (bit0 PTT, 1 squelch open, 2 TX inhibited, 3 charging, 4 sourcing, 5 fault, 6 radio ok, 7 host alive), rssi_dbm i8, channel u8, batt_mv u16, batt_ma i16, soc_pct u8, temp_c i8, vbus_mv u16, vbus_ma i16 (+ = sourcing), pd_role u8 (bit0 source, 1 DFP, 2 contract, 3 partner DRP), pd_mv u16, pd_ma u16, tx_remaining_s u16 |
| 0x92 | DATA_RX | raw AX.25 frame as received on FM (for logging / KISS-style apps) |
| 0x93 | MSG_RX | transport u8, from char[10], id char[6], text |
| 0x94 | MSG_STATUS | id u16, state u8 (1 pending, 2 sent waiting ack, 3 acked, 4 failed) |
| 0x95 | LORA_RX | radio u8, rssi_dbm i16, snr_db i8, raw payload |
| 0x96 | APRS_RX | transport u8, src char[10], kind u8 (1 position, 5 status), lat_e7 i32, lon_e7 i32, course i16, speed_kmh u16, comment/text |
| 0x7F | NAK | same layout as ACK, for malformed frames |

STATUS is 29 bytes: the 21 bytes above followed by mode u8, profile u8,
dv_flags u8 (bit0 enabled, bit1 in sync), lora_flags u8 (bit0 433 listening,
bit1 433 TX, bit2 868 listening, bit3 868 TX), freq_hz u32.

Rules:

- Every frame from the host refreshes the keep-alive. While PTT is keyed the
  host must send something (AUDIO_TX frames do) at least every `ttl_ms`,
  default 2 s, or the transmitter is released (R8).
- STATUS is pushed at 1 Hz and immediately on PTT/squelch/fault changes.
- AUDIO_RX is sent only while the squelch is open (or always, if the app
  sets squelch 0), so an idle link carries 21 bytes per second.
- Audio frames carry no timestamps; the 20 ms cadence and `seq` are enough
  for a jitter buffer of 2-3 frames on the phone (40-60 ms, U2).
- SET_CHANNEL is refused with BUSY while transmitting.

## 6.4 Bandwidth and latency

PCM 16 kHz x 16 bit = 256 kbit/s per direction plus 1.3 % framing; full-speed
bulk delivers > 8 Mbit/s, so the bus is never the bottleneck. Latency budget
phone-mic to RF: 20 ms frame + USB < 2 ms + DAC ring 20 ms = ~42 ms; RF to
phone speaker: ADC ring 20 ms + USB + Android AudioTrack fast path ~20 ms =
~45 ms.

## 6.5 Security and robustness

- The device never executes anything from the host; all values are range
  checked (`pmr446_validate`) and the radio is only ever programmed to the 16
  legal channels (R1).
- A host cannot disable the time-out, the thermal inhibit or the ERP cap.
- Malformed frames are answered with NAK and counted; counters are exposed
  on the debug console.
