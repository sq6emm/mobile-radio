# 1. Requirements

IDs are stable; later documents refer to them. "Shall" = required for v1,
"should" = planned, "may" = optional.

## 1.1 Use cases

- **UC1 Charged phone with radio**: the pack sits on the back of the phone,
  tops it up, and the phone app gives push-to-talk voice to other PMR446
  radios (any brand) on the chosen channel.
- **UC2 Standalone walkie-talkie**: no phone, or phone asleep. A PTT button,
  a small speaker and a microphone on the pack work on the last channel.
- **UC3 Plain power bank**: charge the phone, be charged from a USB-C charger.
- **UC4 Group text (phase 2)**: short text messages between packs over the air.

## 1.2 Radio

| ID | Requirement | Rationale / acceptance |
|----|-------------|------------------------|
| R1 | Shall operate on the 16 PMR446 analogue channels, 446.00625 + (n-1) x 12.5 kHz MHz, FM, 12.5 kHz channel spacing. | ECC/DEC/(15)05, EN 300 296. Verified with a service monitor. |
| R2 | Shall not exceed 500 mW ERP on any channel, including tolerance and temperature. | Legal limit. Firmware selects the module power level from the measured antenna gain (docs/05). |
| R3 | Shall use an integral (non-detachable, no user-accessible connector) antenna. | Mandatory for PMR446. An internal RF test switch is allowed and provided. |
| R4 | Shall support CTCSS (38 tones) encode/decode and should support DCS. | Interoperability with consumer PMR446 radios. |
| R5 | Shall meet EN 300 296 spurious emission limits: <= -36 dBm below 1 GHz, <= -30 dBm above 1 GHz, conducted at the antenna port. | External 7-pole LPF (docs/04). |
| R6 | Receiver sensitivity should be <= -116 dBm for 12 dB SINAD at the antenna port including filter loss. | Module -124 dBm typical minus ~1-3 dB front-end loss. |
| R7 | Shall keep receiving (desense <= 6 dB) while the attached phone transmits at full power on LTE/5G bands 700 MHz and above, with the pack and phone stacked. | Coexistence is the point of the product. BPF option and shielding (docs/04). |
| R8 | Transmit time-out shall be 180 s with 10 s cool-down; a lost host shall release PTT within 2 s. | Stuck-transmitter protection. |
| R9 | Should provide a busy-channel lockout option. | Courtesy, and required by some national rules for data. |

## 1.3 Power and USB-C

| ID | Requirement | Rationale / acceptance |
|----|-------------|------------------------|
| P1 | Shall contain a 2S Li-ion battery of about 30 Wh (<= 100 Wh). | Charges a phone ~1.3x; carry-on compliant. |
| P2 | Shall source USB PD 5 V/3 A and 9 V/2 A (18 W) to the phone, and should source 12 V/1.5 A. | Covers phones' normal charge profiles. |
| P3 | Shall sink 5/9/15 V PD at up to 27 W to charge itself, and 5 V/1.5 A from legacy ports. | 0.5 C charge of 4 Ah cells. |
| P4 | Shall be the PD power source and USB data device (UFP) simultaneously when attached to a PD phone; shall request the necessary PR_Swap / DR_Swap and fall back gracefully. | USB Type-C/PD roles (docs/03). |
| P5 | Shall stop sourcing power below a configurable reserve (default 20 %) so the radio stays usable; shall resume with hysteresis. | Radio has priority over phone charging. |
| P6 | Shall keep the radio powered from the battery while charging. | Power-path charger. |
| P7 | Radio rail shall be 4.0 V +/- 3 % with < 10 mVpp ripple at the module during RX; TX current step of 0.8 A shall not drop it below 3.6 V. | Module supply spec 3.3-4.5 V; noise into the receiver. |
| P8 | Shall survive USB-C faults: VBUS short, reverse current, 20 V on VBUS while sourcing, CC short to VBUS. | Standard PD source/sink protections. |
| P9 | Battery shall have primary and secondary protection (OV, UV, OC, OT) and a fuel gauge with +/- 5 % SoC. | IEC 62133-2, UN 38.3. |

## 1.4 Phone interface

| ID | Requirement | Rationale / acceptance |
|----|-------------|------------------------|
| U1 | Shall enumerate as a USB 2.0 full-speed device with a vendor-specific interface carrying the MRP protocol (docs/06). | Works with Android USB Host API, Linux, Windows (WinUSB), macOS. |
| U2 | Shall stream 16 kHz, 16-bit mono PCM in both directions with end-to-end latency < 60 ms. | Natural PTT conversation. |
| U3 | Should optionally enumerate a USB Audio Class 1.0 headset profile on command, for hosts without a native app (iOS). | Voice without MRP; controls then on the pack. |
| U4 | Shall report battery state, charging state, PD contract, RSSI, squelch state, PTT state and faults at >= 1 Hz. | App UI. |
| U5 | Shall accept channel, CTCSS/DCS, squelch, power level, volume, filter and charging policy settings. | App UI. |
| U6 | Shall not require a driver on Android or a kernel module on Linux. | Plug and play. |

## 1.5 Mechanical, thermal, safety

| ID | Requirement | Rationale / acceptance |
|----|-------------|------------------------|
| M1 | Shall be <= 165 x 78 x 15 mm excluding the antenna stub, with a short USB-C cable to the phone port. | Fits large phones; removable. |
| M1b | Shall attach to the phone with a magnet ring following the Qi2 Magnetic Power Profile / MagSafe ring geometry, including the orientation magnet, with >= 8 N shear hold. | Snaps onto any ring-equipped phone or case in the correct orientation, keeping the antenna vertical and the cable aligned with the port (docs/09). |
| M2 | Antenna shall be vertically polarised when the phone is held upright. | Other PMR446 radios are vertical; cross-pol costs 10-20 dB. |
| M3 | Surface temperature shall stay < 45 C at 18 W sourcing plus 10 % TX duty at 25 C ambient. | EN 62368-1 touch temperature, user comfort. |
| M4 | Shall meet EN 62368-1 (safety), EN 301 489-1/-5 (EMC), EN 300 296 (radio), EN 50566 / EN 62209 (RF exposure) for CE under RED 2014/53/EU. | Market access (docs/10). |
| M5 | Should be IP52 (dust, dripping) with the USB-C port open. | Outdoor use. |

## 1.6 Out of scope for v1

- Digital voice (dPMR446 / DMR446), repeaters, higher power, other bands.
- Bluetooth or Wi-Fi links to the phone (USB only; no extra radios near the receiver).
- Qi wireless charging (coil area and EMI conflict with the antenna and receiver).
