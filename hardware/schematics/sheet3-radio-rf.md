# Sheet 3: radio module and RF chain

## U10 SA818U-V4 (16-pin castellated; names per NiceRF datasheet, **VERIFY**)

| Pin | Name | Connection |
|-----|------|------------|
| 1 | ANT | RF_MOD -> C_dc 100 pF -> LPF C1 |
| 2,3 | GND | ground, many vias |
| 4 | PTT | PB0 through 1 k, 100 pF to GND; 10 k pull-up to +4V0 (inactive high) |
| 5 | PD | PB1 through 1 k (low = power down) |
| 6 | H/L | PB2 through 1 k (high = 1 W, low = 0.5 W; **VERIFY** polarity) |
| 7 | VCC | +4V0, 100 uF + 100 nF + 1 nF at pin |
| 8 | GND | |
| 9 | MIC_IN | from sheet 4 TX attenuator; 100 ohm + 1 nF at pin |
| 10 | GND | |
| 11 | AF_OUT | to sheet 4 RX buffer; 1 nF at pin |
| 12 | SQ | PB10 through 1 k; 100 pF; pull-up 10 k |
| 13 | NC | |
| 14 | RXD | RADIO_TX from MCU through 1 k |
| 15 | TXD | RADIO_RX to MCU through 1 k |
| 16 | NC / POW (SA868) | |

## LPF (values from hardware/rf/filter_design.py)

```
RF_MOD --C_dc 100p-- node1 --L2 22n-- node2 --L4 24n-- node3 --L6 22n-- RF_LPF
                      |               |               |               |
                    C1 7.5p         C3 13p          C5 13p          C7 7.5p
                      |               |               |               |
                     GND             GND             GND             GND
```

Each shunt capacitor has two ground vias; the inductors are placed in a
line with 1 mm clearance, alternating orientation to reduce mutual coupling.

## Optional BPF (option A footprint, bypass with 0 ohm 0402 links when unused)

```
RF_LPF --Ce 2.2p-- R1 --Cc 0.7p-- R2 --Ce 2.2p-- RF_BPF
                   |              |
             L 12n || C 6.8p + trimmer pads
                   |              |
                  GND            GND
```

Option B (RX-only SAW): two PE4259 SPDT footprints, control from PTT buffered
by a 74LVC1G04; SAW footprint 3.0 x 3.0 mm; TX branch is a 0 ohm link.

## Test switch, match, ESD, antenna

```
RF_BPF -> J2 MM8130-2600 (common in, antenna out, probe port) -> RF_TEST
RF_TEST --L_M1 shunt to GND (DC path / ESD) -- C_M1 series -- RF_ANT
                                               |
                                             C_M2 shunt (NP by default)
RF_ANT -> D2 PESD0402-140 to GND -> antenna base pin ANT1
```

Guard ring of ground vias around the whole RF area; shield can SHLD1 over the
module, LPF and BPF; the match and the test switch sit just outside the can
so they can be probed.
