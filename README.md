# mobile-radio

A USB-C battery pack with a built-in UHF radio. It snaps to the back of a
phone with a Qi2/MagSafe-style magnet ring, charges the phone over USB-C
Power Delivery, and at the same time appears to the phone as a USB device
that gives an app push-to-talk voice on the licence-free PMR446 band, and,
for licensed amateurs, the 70 cm band with APRS, text messaging, LoRa
(433 and 868 MHz) and FreeDV digital voice. A Digirig-compatible sound-card
profile lets existing digital-mode programs and iPhones use it without the
app. It also works alone, as a power bank and as a basic walkie-talkie.

```
   +---------------------------- phone ----------------------------+
   |  app: PTT, channel, CTCSS, RSSI, battery, charging control    |
   +----------------------------- USB-C ---------------------------+
             | PD: pack = power SOURCE, data = UFP (device)
   +---------------------------- pack -----------------------------+
   | USB-C PD/charger  <->  2S Li-ion  ->  4.0 V rail -> SA818 UHF |
   | STM32F405 (USB device, audio DMA, control)                    |
   | SA818 ANT -> 7-pole LPF -> (opt. BPF) -> match -> integral ant |
   +---------------------------------------------------------------+
```

## What is in this repository

| Path | Contents |
|------|----------|
| `docs/01-requirements.md` | Numbered requirements with rationale and acceptance criteria |
| `docs/02-system-architecture.md` | Block diagram, power budget, interfaces, key design decisions |
| `docs/03-power-and-usb-pd.md` | Battery, charger, rails, USB-C PD role negotiation |
| `docs/04-rf-front-end.md` | Radio module, harmonic LPF, optional BPF, coexistence with the phone's radios |
| `docs/05-antenna.md` | Integral antenna options, matching, ERP budget, range |
| `docs/06-usb-interface-and-protocol.md` | USB descriptors and the MRP control/audio protocol |
| `docs/07-audio-path.md` | Analog audio chain and audio filtering |
| `docs/08-firmware.md` | Firmware architecture and safety interlocks |
| `docs/09-mechanical-and-thermal.md` | Enclosure, stack-up, heat |
| `docs/10-regulatory-and-safety.md` | PMR446 rules, RED/EMC/safety standards, battery transport |
| `docs/11-test-plan.md` | Bring-up, RF conformance, PD interoperability, battery, data/LoRa/DV tests |
| `docs/12-data-and-aprs.md` | 1200 Bd AFSK, AX.25, APRS beacons, text messages, channel arbitration |
| `docs/13-lora.md` | Two SX1262 modules, antenna sharing, LoRa-APRS, presets, duty limits |
| `docs/14-digital-voice.md` | FreeDV 2400B / Codec2 through the FM path |
| `hardware/rf/filter_design.py` | LPF/BPF synthesis and lossy simulation (pure Python + numpy) |
| `hardware/rf/antenna_calc.py` | Helix/IFA sizing, L-match, ERP and link budget |
| `hardware/rf/results/` | Generated filter tables and antenna report |
| `hardware/schematics/` | Sheet-by-sheet connection tables for schematic capture |
| `hardware/bom.csv` | Bill of materials, key parts with alternatives |
| `firmware/` | Portable C modules (protocol, band plan, radio driver, PD policy, PTT guard, AFSK/AX.25/APRS, messaging, beacons, arbiter, resampler, SX1262 driver, FreeDV glue), application, host link check, unit tests |
| `firmware/third_party/codec2` | Codec2 / FreeDV library (git submodule, LGPL 2.1) |
| `tools/mrp.py`, `tools/afsk_wav.py` | Python reference implementation of the protocol and of AX.25/AFSK, bench CLI, WAV generator |
| `app/README.md` | Phone-side integration notes (Android USB host, audio, PTT) |

## Quick start

```sh
# RF design numbers
python3 hardware/rf/filter_design.py --out hardware/rf/results
python3 hardware/rf/antenna_calc.py report

# firmware unit tests + whole-application link against the host port (gcc only)
git submodule update --init --depth 1   # Codec2 headers
make -C firmware all

# Python implementation cross-checked byte for byte against the C reference vectors
python3 tools/test_mrp.py
python3 tools/afsk_wav.py --src SQ6EMM-7 --info '!5106.50N/01702.25E[test' --out packet.wav
```

## Headline design choices

- **Bands and radio**: PMR446 (16 FM channels, 500 mW ERP, integral antenna)
  for everyone, plus a HAM mode for 430-440 MHz unlocked by a callsign. One
  SA818U/DRA818U UHF module (1 W / 0.5 W) behind a 7-pole low-pass and a
  2-pole 439 MHz band-pass; the helix is tuned to 439 MHz to cover both.
- **LoRa**: two SX1262 modules. 433 MHz shares the helix through an RF switch
  (LoRa-APRS 433.775, pack-to-pack, ham); 868 MHz has its own printed antenna
  (pack-to-pack, Meshtastic EU_868 radio settings as a preset). Voice always
  wins the shared antenna.
- **Data and digital voice**: 1200 Bd AFSK AX.25 with APRS beacons and
  acknowledged text messages (FM and LoRa), FreeDV 2400B digital voice
  through the FM path (HAM only). Audio hardware runs at 48 kHz with flat
  bypass paths so 9600 Bd modes can follow in software.
- **Power**: 2S Li-ion (29.6 Wh), BQ25798 buck-boost charger with OTG output,
  TPS25751 USB-C PD controller. The pack is the power **source** and the data
  **device** at the same time, reached through PD role swaps.
- **Phone link**: profile "app" = one vendor USB interface carrying the MRP
  protocol (control, status, 16 kHz PCM, messages, LoRa, APRS) plus a CDC
  port; profile "sound-card" = UAC2 48 kHz audio plus CDC with RTS as PTT,
  Digirig-compatible, for iOS and for Direwolf/FreeDV/SSTV/VARA on any host.
- **Attachment**: Qi2 MPP / MagSafe-compatible magnet ring with orientation
  magnet, so the pack snaps onto the phone upright (antenna vertical, cable
  aligned with the port), plus a 100 mm USB-C cable.
- **Safety interlocks in firmware**: host keep-alive, 180 s transmit time-out,
  thermal and low-battery inhibit, busy-channel lockout, ERP cap from the
  measured antenna gain.

## Status

Design package and verified software building blocks (all unit tests pass,
the whole application links against a host stub, Python and C agree byte for
byte). No PCB has been fabricated yet; the STM32 port, the KiCad board and the
Android app are the next phases. Items that must be confirmed on hardware are
marked **VERIFY** throughout the docs. The FreeDV glue is tested against a
stand-in; the real Codec2 round trip still has to be built on a developer
machine (docs/08, 8.6).
