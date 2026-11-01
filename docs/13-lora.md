# 13. LoRa: 433 MHz and 868 MHz

## 13.1 Why two modules

There is no production dual-band LoRa module: the SX1262 chip tunes 150-960
MHz, but every module's matching, harmonic filter and PA are built for one
band. Two 22 dBm single-band modules (E22-400M22S and E22-900M22S class,
SX1262 inside, TCXO, ~6 EUR each) are simpler and lower risk than a custom
dual-band front end on a first board.

| | LoRa 433 | LoRa 868 |
|-|----------|----------|
| Module | E22-400M22S class (SX1262 + TCXO, +22 dBm) | E22-900M22S class |
| Antenna | shares the 439 MHz helix through an SPDT switch (PE4259) | own printed inverted-F at the opposite top corner (docs/05) |
| Use | LoRa-APRS 433.775 MHz, pack-to-pack 433.900 MHz, ham experiments, Meshtastic EU_433 PHY | pack-to-pack and Meshtastic EU_868 PHY at 869.525 MHz, ISM 500 mW band |
| Legal power | PMR/ISM mode: 10 mW ERP (EN 300 220, 433.05-434.79 MHz); HAM mode: +22 dBm | 869.4-869.65 MHz: 500 mW ERP, 10 % duty; 868.0-868.6: 25 mW, 1 % |
| Concurrency | alternates with FM (voice has priority); listens only when the user prefers the LoRa receiver over FM | independent of FM; RX continuous |

## 13.2 Antenna sharing on 433 MHz

```
SA818 -> LPF ----\
                  SPDT (PE4259) -> BPF 439/50 -> test switch -> match -> helix
LoRa433 module --/
```

- The switch is controlled by the arbiter (`arbiter_antenna()`): FM during
  FM TX and by default in RX; LoRa 433 during its TX and when the user
  selects `rx_pref = LORA433`.
- Isolation: PE4259 gives ~25 dB at 450 MHz. With 1 W FM TX the LoRa module
  would see about +5 dBm, inside the SX1262 absolute maximum (+10 dBm) but
  not comfortable. The firmware holds the LoRa 433 module in standby during
  FM TX, and the PCB has a footprint for a shunt PIN diode limiter
  (BAP64-02 class) on the LoRa branch. Measured in T-L2.
- The BPF (439 MHz, 50 MHz wide) is in the common path, so LoRa-APRS at
  433.775 MHz passes with ~1.2 dB loss and the LoRa receiver gets the same
  protection from the phone's cellular transmitter as the FM receiver.
- Consequence to accept: continuous LoRa-APRS or mesh *reception* on 433 MHz
  mutes FM reception. If that matters more than board space, a second 433
  antenna (another printed IFA) removes the restriction; the schematic keeps
  the option as a stuffing variant.

## 13.3 Radio settings (`lora_sx1262.c` presets)

| Preset | Frequency | SF/BW/CR | Sync | Preamble | Power | Time on air, 50 B |
|--------|-----------|----------|------|----------|-------|-------------------|
| LoRa-APRS (EU) | 433.775 MHz | SF12 / 125 kHz / 4:5 | 0x12 | 8 | 22 dBm (HAM) | 2.3 s |
| Pack-to-pack 433 | 433.900 MHz | SF9 / 125 kHz / 4:5 | 0x12 | 8 | 10 dBm (ISM) | 0.33 s |
| Pack-to-pack 868 | 869.525 MHz | SF9 / 125 kHz / 4:5 | 0x12 | 8 | 22 dBm | 0.33 s |
| Meshtastic EU_868 LongFast (PHY only) | 869.525 MHz | SF11 / 250 kHz / 4:5 | 0x2B | 16 | 22 dBm | 1.1 s |

The driver sets LDRO automatically when the symbol time exceeds 16 ms, uses
the SX1262 +22 dBm PA table, applies the TX-clamp errata and 140 mA OCP, and
reports RSSI/SNR per packet. Image calibration bands are chosen from the
frequency.

## 13.4 Frame formats

- **LoRa-APRS**: `'<' 0xFF 0x01` + `SRC>DST,PATH:INFO` (TNC2 text). INFO is
  ordinary APRS, so the same `aprs.c`/`msg.c` code produces positions,
  messages and acks; iGates forward them to the APRS-IS. (`lora_aprs.c`)
- **Pack-to-pack**: the same LoRa-APRS framing on the pack frequencies, with
  nicknames in PMR mode. One code path, two bands.
- **Meshtastic**: the PHY preset lets the module hear and be heard on a
  Meshtastic channel at the radio level, but the Meshtastic packet format
  (protobuf header, AES channel key, flooding/routing, node DB) is not
  implemented. Doing that properly is a project of its own; it is listed as a
  later phase so nobody expects mesh interoperability from v1.

## 13.5 Duty cycle and power enforcement

- 868 MHz: the firmware accumulates time on air per hour per sub-band and
  refuses TX beyond 10 % (869.4-869.65) or 1 % (868.0-868.6).
- 433 MHz in PMR/ISM mode: power capped at +10 dBm conducted (0 dBd antenna
  assumption; adjust from the measured helix gain) to stay under 10 mW ERP;
  HAM mode lifts the cap to +22 dBm.
- Both LoRa transmitters are inhibited by the same thermal and battery
  interlocks as the FM PA.

## 13.6 Hardware notes for the schematic

- Each module: 3.3 V (+3V3R rail, 150 mA TX each), SPI (shared bus, separate
  NSS), BUSY, DIO1 (IRQ), NRST, TXEN/RXEN (driven by the MCU via the driver's
  `rf_switch` callback; DIO2 switch mode is also supported).
- Keep the 868 IFA >= 30 mm from the helix base and >= 20 mm from the magnet
  ring; a 10 pF series capacitor pad and a shunt inductor pad for tuning.
- 433 branch: 50 ohm CPW to the SPDT; optional shunt PIN limiter footprint.
- Shield both modules (they come canned) and route their SPI away from the
  audio front end.
