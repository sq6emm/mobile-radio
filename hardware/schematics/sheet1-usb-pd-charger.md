# Sheet 1: two USB-C ports, PD controllers, charger, source converter, battery

## J1 PHONE port (USB-C 16-pin)

| Pin | Net | Notes |
|-----|-----|-------|
| A4/B4, A9/B9 VBUS | VBUS_PHONE | 4 pins tied; 10 uF + 1 uH + 10 uF pi, SMBJ24A TVS |
| A5 CC1, B5 CC2 | CC1_P, CC2_P | to TPS25751 CC1/CC2 |
| A6/B6 D+, A7/B7 D- | USB_DP, USB_DM | both orientations tied; CM choke FL_CM1; TVS D1; 22 ohm to MCU |
| A8/B8 SBU | NC | |
| shell | GND via 1 nF || 1 MOhm + ferrite bead | |

## J3 CHARGE port (USB-C 16-pin, power only)

| Pin | Net | Notes |
|-----|-----|-------|
| A4/B4, A9/B9 VBUS | VBUS_CHARGE | pi filter, SMBJ24A TVS |
| A5 CC1, B5 CC2 | CC1_C, CC2_C | to STUSB4500 CC1/CC2 |
| D+/D-, SBU | NC | no data on this port |
| shell | as J1 | |

## U2 TPS25751D (PHONE port)

| Pin group | Net | Notes |
|-----------|-----|-------|
| CC1, CC2 | CC1_P, CC2_P | |
| PP_EXT gate drive | Q2 back-to-back N-FETs: VBUS_SRC (TPS55288 output) -> VBUS_PHONE | source path |
| Sink path | internal: VBUS_PHONE -> VIN_P | to Q3 (ACFET1) -> BQ25798 VAC1 |
| I2C_SDA/SCL, IRQ | I2C1, PD_IRQ | address 0x21 |
| LDO_3V3, VIN_3V3 | +3V3D | |
| Configuration | ADCIN straps + I2C | DRP, Try.SRC, source PDOs 5V3A 9V2A 12V1.5A, sink PDOs 5V3A 9V3A 15V2A, DR_Swap to UFP |

## U20 STUSB4500 (CHARGE port)

| Pin | Net | Notes |
|-----|-----|-------|
| CC1, CC2 | CC1_C, CC2_C | |
| VBUS_EN_SNK | gate of Q4 (ACFET2 pair) via BQ25798 ACDRV2 logic, or directly | VBUS_CHARGE -> VAC2 |
| VSYS / VDD | VBUS_CHARGE (internal LDO) and +3V3D | |
| SDA/SCL/ALERT | I2C1 (address 0x28), CHG_PD_IRQ | contract read-back |
| NVM PDOs | PDO3 20 V 2.25 A, PDO2 15 V 3 A, PDO1 5 V 3 A | programmed once at production |

## U19 TPS55288 (PHONE port source converter)

| Pin | Net |
|-----|-----|
| VIN | VSYS (2 x 22 uF) |
| SW1/SW2, BST1/BST2 | L4 2.2 uH 6 A |
| VOUT | VBUS_SRC (3 x 22 uF 25 V) -> Q2 -> VBUS_PHONE |
| EN | SRC_EN (MCU) |
| SDA/SCL | I2C1 (address 0x74/0x75 by strap) |
| FB / CDC | per datasheet; output set by I2C (5.0 / 9.0 / 12.0 V) |
| ISP/ISN | 10 mOhm sense for the I2C current limit |

## U3 BQ25798 (charger, two inputs, sink only)

| Pin | Net | Notes |
|-----|-----|-------|
| VAC1 | VIN_P via Q3 ACFET1/RBFET1 pair, driven by ACDRV1 | PHONE port input; held OFF while sourcing (firmware) |
| VAC2 | VBUS_CHARGE via Q4 ACFET2/RBFET2 pair, driven by ACDRV2 | CHARGE port input; preferred input |
| VBUS | common node after the two pairs | |
| SW1/SW2, BTST1/2 | L1 2.2 uH 8 A | |
| SYS | VSYS | 4 x 22 uF 25 V |
| BAT | VBAT+ | through protector and 5 mOhm sense R_SNS |
| SDA/SCL/INT | I2C1, CHG_IRQ | address 0x6B |
| CE | CHG_CE (MCU) | pull-down enables charging |
| ILIM_HIZ | resistor to GND 1.5 A default; MCU overrides by I2C | |
| TS | NTC1 (cells) divider | 0-45 C charge window |
| OTG / QON | OTG tied off (not used); QON to MCU for ship mode | |
| REGN, PMID | per datasheet caps | |
| D+/D- | NC | |

## Battery

| Item | Connection |
|------|------------|
| BT1 cells 2S | VBAT+ , CELL_MID, BAT- |
| U5 S-8252 | VDD = VBAT+, VC = CELL_MID, VSS = BAT-, DO/CO drive Q1 dual N-FET in the BAT- return to GND; VM through 2 k |
| U6 BQ7718 | senses each cell; OUT drives F1 chemical fuse heater between VBAT+ and the pack |
| U21 BQ29209 | 2S balancer across VBAT+ / CELL_MID / BAT- |
| U4 BQ34Z100-G1 | BAT sense VBAT+ via divider, SRP/SRN across R_SNS 5 mOhm in the GND return, TS = NTC1, I2C address 0x55, ALERT to MCU |
| R_SNS | 5 mOhm 2 W, in BAT- return before Q1 |
