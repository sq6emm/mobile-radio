# Sheet 1: USB-C, PD controller, charger, battery

## J1 USB-C receptacle (16-pin)

| Pin | Net | Notes |
|-----|-----|-------|
| A4/B4, A9/B9 VBUS | VBUS | 4 pins tied; 10 uF + 1 uH + 10 uF pi, SMBJ24A TVS |
| A5 CC1, B5 CC2 | CC1, CC2 | to TPS25751 CC1/CC2 |
| A6/B6 D+, A7/B7 D- | USB_DP, USB_DM | both orientations tied; CM choke FL_CM1; TVS D1; 22 ohm to MCU |
| A8/B8 SBU | NC | |
| shell | GND via 1 nF || 1 MOhm + ferrite bead | |

## U2 TPS25751D

| Pin group | Net | Notes |
|-----------|-----|-------|
| CC1, CC2 | CC1, CC2 | |
| PP_HV / PP_EXT gate drive | VBUS source path FET Q2 (back-to-back N-FETs) | source path from BQ25798 OTG output `VBUS_OTG` to `VBUS` |
| Sink path | internal, VBUS to `VIN_CHG` | to BQ25798 VBUS pin |
| I2C_SDA/SCL, IRQ | I2C1_SDA, I2C1_SCL, PD_IRQ | address 0x21 (strap ADCIN) |
| GPIO0 | CHG_OTG_EN | optionally directly enables BQ25798 OTG |
| LDO_3V3, VIN_3V3 | +3V3D | |
| VBUS sense | VBUS | |
| Configuration | ADCIN1/ADCIN2 straps + I2C from MCU | DRP, Try.SRC, source PDOs 5V3A 9V2A (12V1.5A optional), sink PDOs 5V3A 9V3A 15V2A |

## U3 BQ25798

| Pin | Net | Notes |
|-----|-----|-------|
| VBUS | VIN_CHG | input from PD sink path |
| VAC1/VAC2 | VIN_CHG | ACFET not used (TPS25751 handles) |
| SW1/SW2, BTST1/2 | L1 2.2 uH 8 A | |
| SYS | VSYS | 4 x 22 uF 25 V |
| BAT | VBAT+ | through protector and 5 mOhm sense R_SNS |
| SDA/SCL/INT | I2C1, CHG_IRQ | address 0x6B |
| CE | CHG_CE (MCU) | pull-down enables charging |
| ILIM_HIZ | resistor to GND 1.5 A default; MCU overrides by I2C | |
| TS | NTC1 (cells) divider | 0-45 C charge window |
| STAT | LED through MCU or direct | |
| OTG / QON | MCU GPIO | OTG request; QON for ship mode |
| REGN, PMID | per datasheet caps | PMID 2 x 22 uF |
| D+/D- | NC | BC1.2 detection unused (PD controller instead) |

OTG output: `VBUS_OTG` = BQ25798 VBUS pin in OTG mode, routed through Q2
source-path FETs controlled by TPS25751.

## Battery

| Item | Connection |
|------|------------|
| BT1 cells 2S | VBAT+ , CELL_MID, BAT- |
| U5 S-8252 | VDD = VBAT+, VC = CELL_MID, VSS = BAT-, DO/CO drive Q1 dual N-FET in the BAT- return to GND; VM through 2 k |
| U6 BQ7718 | senses each cell; OUT drives F1 chemical fuse heater between VBAT+ and the pack |
| U4 BQ34Z100-G1 | BAT sense VBAT+ via divider, SRP/SRN across R_SNS 5 mOhm in the GND return, TS = NTC1, I2C address 0x55, ALERT to MCU |
| R_SNS | 5 mOhm 2 W, in BAT- return before Q1 |

Balancing: BQ25798 has none; add a BQ29209 2S balancer (or a gauge with
balancing) across CELL_MID. Marked as design option; cells matched at
assembly.
