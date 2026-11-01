# mobile-radio

A USB-C battery pack with a built-in PMR446 two-way radio. It attaches to the
back of a phone, charges the phone over USB-C Power Delivery, and at the same
time appears to the phone as a USB device that gives an app push-to-talk voice
(and later short data messages) on the licence-free PMR446 band. It also works
alone, as a plain power bank and as a basic walkie-talkie.

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
| `docs/11-test-plan.md` | Bring-up, RF conformance, PD interoperability, battery tests |
| `hardware/rf/filter_design.py` | LPF/BPF synthesis and lossy simulation (pure Python + numpy) |
| `hardware/rf/antenna_calc.py` | Helix/IFA sizing, L-match, ERP and link budget |
| `hardware/rf/results/` | Generated filter tables and antenna report |
| `hardware/schematics/` | Sheet-by-sheet connection tables for schematic capture |
| `hardware/bom.csv` | Bill of materials, key parts with alternatives |
| `firmware/` | Portable C modules (protocol, channel plan, radio driver, PD policy, PTT guard), app skeleton, unit tests |
| `tools/mrp.py` | Python reference implementation of the protocol and a bench CLI |
| `app/README.md` | Phone-side integration notes (Android USB host, audio, PTT) |

## Quick start

```sh
# RF design numbers
python3 hardware/rf/filter_design.py --out hardware/rf/results
python3 hardware/rf/antenna_calc.py report

# firmware unit tests and application syntax check (host gcc, no toolchain needed)
make -C firmware all

# protocol tool self-test (shares its test vector with the C test)
python3 tools/test_mrp.py
```

## Headline design choices

- **Band and radio**: PMR446, 16 analogue FM channels at 12.5 kHz, 500 mW ERP,
  integral antenna as the regulation demands. Radio is an SA818U/DRA818U UHF
  module (1 W / 0.5 W, UART controlled) behind a 7-pole Chebyshev low-pass
  filter and an optional 2-pole band-pass.
- **Power**: 2S Li-ion (29.6 Wh), BQ25798 buck-boost charger with OTG output,
  TPS25751 USB-C PD controller. The pack is the power **source** and the data
  **device** at the same time, reached through PD role swaps.
- **Phone link**: one vendor-specific USB interface carrying the MRP protocol
  (control, status, 16 kHz PCM both ways). No USB Audio Class by default, so
  Android does not hijack the phone's audio routing; a UAC profile can be
  enabled for iOS/generic hosts.
- **Attachment**: Qi2 MPP / MagSafe-compatible magnet ring with orientation
  magnet, so the pack snaps onto the phone upright (antenna vertical, cable
  aligned with the port), plus a 100 mm USB-C cable.
- **Safety interlocks in firmware**: host keep-alive, 180 s transmit time-out,
  thermal and low-battery inhibit, busy-channel lockout, ERP cap from the
  measured antenna gain.

## Status

Design package and verified software building blocks. No PCB has been
fabricated yet. See `docs/11-test-plan.md` for the bring-up order. Items that
must be confirmed on hardware are marked **VERIFY** throughout the docs.
