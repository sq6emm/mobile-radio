# 12. Packet data, APRS and text messages over FM

Everything in this chapter rides on the ordinary FM voice path of the SA818:
audio tones into MIC_IN, audio tones out of AF_OUT. No hardware beyond the
voice design is required; the firmware modules are `afsk`, `ax25`, `aprs`,
`msg`, `beacon` and `arbiter` (all host-tested).

## 12.1 Physical layer: Bell 202 AFSK, 1200 Bd

| Item | Value |
|------|-------|
| Tones | mark 1200 Hz, space 2200 Hz, continuous phase |
| Rate | 1200 Bd, NRZI (a 0 bit toggles the tone, a 1 bit keeps it) |
| Deviation | set the DAC level so that the tones give ~3 kHz peak deviation (T-A3); the module's pre-emphasis stays on, as with every handheld TNC set-up |
| TX sequencing | key PA, 300 ms of flags (TX delay, 45 flags), frame, 3 flags, unkey. TX delay is configurable 100-500 ms |
| RX | 48 kHz ADC -> `resample` to 16 kHz -> `afsk_demod` (band-pass, one-bit quadrature correlators at both tones, PLL bit sync) -> `hdlc_rx` (NRZI, de-stuffing, FCS) |

Simulated performance of the demodulator (`tests/test_afsk.c`, white noise
over the whole 16 kHz band, 100 packets of 90 bytes):

| SNR in 16 kHz band | equivalent in a 3 kHz voice band | packets decoded |
|--------------------|----------------------------------|-----------------|
| 20 dB | 24 dB | 100 % |
| 10 dB | 14 dB | 100 % |
| 6 dB | 10 dB | 100 % |
| 4 dB | 8 dB | 98 % |
| 2 dB | 6 dB | 63 % |
| 0 dB | 4 dB | 1 % |

It also tolerates +/-2 % sample-rate error and 35 dB of level variation. Real
FM channels add distortion and fading; expect packets to decode down to about
the 12 dB SINAD point of the receiver, which is what hardware TNCs achieve.

## 12.2 Link layer: AX.25 UI frames

Standard AX.25 v2 unnumbered information frames: destination, source, up to
8 digipeaters, control 0x03, PID 0xF0, information field, FCS CRC-16/X-25,
HDLC flags and bit stuffing. Destination ("tocall") is `APZMRP`
(experimental prefix APZ; register an APxxxx tocall before release). Default
path `WIDE1-1` (one hop) to be polite on the VHF/UHF APRS network; the app can
set `WIDE1-1,WIDE2-1` or none.

## 12.3 Application layer: APRS

| Function | Format | Where |
|----------|--------|-------|
| Position | `!DDMM.hhN/DDDMM.hhE[CSE/SPD/A=aaaaaa comment`, symbol table/code configurable (default `/[` jogger, `/>` car when speed > 20 km/h) | `aprs_encode_position` |
| Message | `:ADDRESSEE:text{id}` (id present = ack requested) | `aprs_encode_message` |
| Ack / reject | `:ADDRESSEE:ackID` / `rejID` | `aprs_encode_ack` |
| Decoder | uncompressed positions with/without timestamp, course/speed, messages, acks, status; compressed and Mic-E positions are reported as unknown (phase 2) | `aprs_decode` |

Position source: the phone sends `POSITION` frames (lat/lon/alt/speed/course)
from its GNSS over MRP; the pack has no GNSS of its own. In standalone use the
last known position is beaconed with an age flag in the comment.

## 12.4 Text messages (`msg.c`)

- Outbox of 8, each message gets an id 1..99999, sent with `{id}`.
- Retries after 10, 20 and 40 s; after 3 attempts the message is marked
  failed and the app is told (`MSG_STATUS`).
- Incoming messages addressed to our identity (callsign-SSID in HAM mode,
  nickname in PMR mode) are delivered to the app (`MSG_RX`) and acknowledged;
  duplicates (same sender and id within the last 16) are acknowledged again
  but flagged so the app does not show them twice.
- Messages to a group use addressee `ALL` or a group nickname and no id.
- The same layer is used over LoRa (docs/13); the `transport` field of
  `SEND_MSG` selects FM, LoRa 433 or LoRa 868.

## 12.5 Beacons (`beacon.c`)

Fixed interval (default 10 min) or SmartBeaconing: 30 min when stationary,
60 s at >= 90 km/h, interpolated in between, plus a beacon on a course change
larger than 28 deg + 255/speed after at least 15 s. A hard floor of 30 s
between rate beacons protects the channel.

## 12.6 Channel arbitration (`arbiter.c`, `ptt_guard.c`)

- Voice (phone or button) > digital voice > FM data > LoRa 433 on the shared
  antenna path. Voice pre-empts an ongoing data frame immediately (the frame
  is lost and retried later); data never interrupts voice and is refused
  while voice is keyed.
- Data TX uses the same `ptt_guard` as voice (source `PTT_SRC_DATA`), so the
  time-out, thermal inhibit and busy-channel lockout apply. Data frames use
  busy-channel detection by default (wait up to 5 s for the squelch to close,
  then random back-off 0.5-2 s).
- Duty cycle: beacons and messages are small (< 1 s of TX); the firmware
  limits data to 10 % averaged over 60 s in PMR mode (country rules vary;
  docs/10).

## 12.7 PMR446 and data

ECC/DEC/(15)05 allows data on PMR446 in general, but several administrations
apply conditions (no store-and-forward, duty cycle, no fixed use). The pack's
data features in PMR mode are therefore: text messages and position
exchange between packs, with the nickname as identity and the 10 % duty
limit. APRS beacons into the amateur network are HAM mode only.

## 12.8 Bench tools

- `tools/afsk_wav.py` writes an AX.25/AFSK packet as a 48 kHz WAV so it can be
  played into a real TNC or Direwolf for an independent decode check, and
  decoded by the C demodulator test.
- `tools/mrp.py msg`, `beacon`, `pos`, `identity` drive the features from a PC.
