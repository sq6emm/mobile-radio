# Schematic sheets

Connection tables for schematic capture (KiCad). Pin names follow the
datasheets; **VERIFY** against the exact revision before layout.

| Sheet | File | Contents |
|-------|------|----------|
| 1 | `sheet1-usb-pd-charger.md` | USB-C, TPS25751, BQ25798, battery, protection, gauge |
| 2 | `sheet2-rails-mcu.md` | +4V0 buck, LDOs, STM32F405, debug, buttons, LEDs |
| 3 | `sheet3-radio-rf.md` | SA818, control lines, LPF, BPF, test switch, match, antenna |
| 4 | `sheet4-audio.md` | buffers, filters, speaker amp, microphone |

Net naming: `VBUS`, `VSYS`, `VBAT+`, `+4V0`, `+3V3D`, `+3V3A`, `GND`,
`AGND` (joined at one point under the MCU), `RF_MOD`, `RF_LPF`, `RF_TEST`,
`RF_ANT`.
