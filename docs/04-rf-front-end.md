# 4. RF front end and filters

## 4.1 Radio module

SA818U-V4 (NiceRF) or DRA818U (Dorji), 400-480 MHz FM transceiver module:

| Parameter | Value (datasheet, **VERIFY** per revision) |
|-----------|---------------------------------------------|
| Supply | 3.3-4.5 V, design uses 4.0 V |
| RF output | 1 W (H) / 0.5 W (L), selected by the H/L pin |
| TX current | <= 750 mA at 1 W |
| RX current | ~60-70 mA; power-down < 1 uA via PD pin |
| Sensitivity | -124 dBm (12 dB SINAD) |
| Channel spacing | 12.5 / 25 kHz, 12.5 kHz used |
| Control | UART 9600 8N1, `AT+DMOSETGROUP`, `AT+DMOSETVOLUME`, `AT+SETFILTER`, `AT+SETTAIL`, `RSSI?` |
| Audio | MIC_IN (electret level), AF_OUT (line level, needs amplifier), built-in pre/de-emphasis, 300 Hz HPF, 3 kHz LPF, CTCSS/DCS |
| Antenna port | 50 ohm pin, internal T/R switch and basic harmonic filter |

The module is a complete radio; everything in this chapter is about making
its single antenna pin legal, clean and robust next to a phone.

## 4.2 Signal chain

```
SA818 ANT -> C_dc -> [LPF: C1 L2 C3 L4 C5 L6 C7] -> [BPF 2-pole, optional] ->
  [RF test switch MM8130] -> [match: shunt L / series C / shunt C pads] -> [ESD] -> antenna feed
```

- 50 ohm coplanar waveguide with ground, 0.3 mm wide / 0.2 mm gap on 0.2 mm
  prepreg to L2 (adjust to stack-up), ground vias every 2 mm along the line.
- All filter parts 0603 (inductors) / 0402 C0G (capacitors), inductor Q >= 60
  at 450 MHz (wirewound ceramic, e.g. Coilcraft 0603HP / Murata LQW18 class).
- RF test switch (Murata MM8130-2600): inserting a probe disconnects the
  antenna and gives a conducted port for all compliance measurements. It is
  under the back cover, not user accessible, so the antenna remains
  "integral" in the regulatory sense.

## 4.3 TX harmonic low-pass filter

Synthesised and simulated by `hardware/rf/filter_design.py` (results in
`hardware/rf/results/filter_summary.md`).

Design: 7th-order Chebyshev, 0.1 dB ripple, shunt-C first, f_c = 500 MHz,
Z0 = 50 ohm.

| Element | Ideal | E24 part | Note |
|---------|-------|----------|------|
| C1, C7 | 7.52 pF | 7.5 pF C0G 0402 | |
| L2, L6 | 22.6 nH | 22 nH 0603 wirewound | |
| C3, C5 | 13.35 pF | 13 pF C0G 0402 | |
| L4 | 25.0 nH | 24 nH 0603 wirewound | |

Simulated with Q_L = 60, Q_C = 300, 0.1 pF inductor parasitic capacitance and
0.4 nH capacitor ESL:

| Frequency | S21 | Comment |
|-----------|-----|---------|
| 446.0-446.2 MHz | -0.8 dB | passband, S11 -16 dB |
| 703 MHz (LTE B28 UL) | -35 dB | receiver protection |
| 832 MHz (LTE B20 UL) | -50 dB | |
| 892 MHz (2nd harmonic) | -56 dB | with module's own ~ -40 dBc: ~ -96 dBc, limit -66 dBc |
| 1338 MHz (3rd harmonic) | < -70 dB (layout floor) | |
| >= 1.7 GHz | < -70 dB (layout floor) | |

Why 500 MHz and not closer to the band: component tolerance (5 % inductors
move the ripple edge by ~2.5 %) and the module's output impedance is not an
ideal 50 ohm; a 12 % guard band keeps the passband loss and match stable. A
cutoff of 540 MHz would save 0.2 dB and lose 7 dB at 700 MHz; the receiver
protection is worth more here.

Spurious budget at the antenna port (EN 300 296, TX): carrier +30 dBm,
limit -36 dBm below 1 GHz (-66 dBc) and -30 dBm above 1 GHz (-60 dBc).
Module 2nd harmonic typically -40 to -50 dBc, 3rd -50 to -60 dBc. After the
LPF both are below -90 dBc, limited in practice by shielding and layout
(~ -70 to -80 dBc). Margin > 10 dB.

## 4.4 Optional band-pass filter (receiver selectivity)

Purpose: the attached phone transmits up to +23 dBm at 700-960 MHz and
1.7-2.7 GHz, 1-3 cm from the pack antenna. Coupling of -20 to -30 dB puts
-7 to +3 dBm into the pack's antenna; the LPF leaves -42 to -32 dBm at 703 MHz,
which is near the module's blocking limit (R7). A 446 MHz band-pass adds
30 dB where it matters, and also attenuates TETRA (380 MHz) and LPD/ISM 433 MHz
traffic that the LPF passes unfiltered.

Design from the script: 2-pole Butterworth, capacitively coupled shunt
resonators, f0 = 446.1 MHz, BW = 40 MHz, L = 12 nH:

| Element | Value |
|---------|-------|
| C_end (in/out, series) | 2.3 pF |
| C_couple | 0.72 pF |
| C_res (each resonator, shunt) | 7.8 pF (+ 2.2-3.3 pF trimmer range on the pads) |
| L_res | 12 nH, Q >= 120 (air-core 4 turns 1 mm wire on 3 mm, or ceramic coax resonator) |

Insertion loss 1.1 dB at Q_u = 120, 0.5 dB with a dielectric (coaxial ceramic)
2-pole filter; rejection -30 dB at 703 MHz, -24 dB at 380 MHz, 1.7 dB at 433.9 MHz.

Caveats that decide the stuffing option:

1. In the common TX/RX path the resonators carry ~1.6 A RMS circulating
   current at 1 W (Q_L = 11). Chip inductors cannot do that; air-core coils
   or a commercial 2-pole dielectric filter for 446 MHz PMR (standard part in
   PMR446 handsets) are required.
2. Alternatively the BPF (or a 446 MHz SAW filter, which can not take TX
   power) goes in a **receive-only branch** behind an SPDT pair
   (PE4259, +34 dBm), switched by the PTT line. That costs 0.7 dB in both
   directions and two more parts but gives the best receiver. The PCB has
   footprints for both; option A (common-path dielectric filter) is the
   default, option B is populated if the coexistence test T-R7 fails.

## 4.5 Coexistence with the phone and the pack's own electronics

Threats to the -120 dBm receiver:

| Source | Mechanism | Mitigation |
|--------|-----------|------------|
| Phone cellular TX 700-960 MHz, +23 dBm | front-end blocking, reciprocal mixing | LPF (35-50 dB), BPF option, antenna at the pack top edge away from the phone's antennas (usually bottom and top edges: keep the pack antenna off-centre, top-left) |
| Phone 1.7-2.7 GHz, 3.5 GHz, Wi-Fi, BT | out-of-band blocking, PA harmonics landing in band? (none of 446 MHz x n falls in these; the reverse, phone TX / n, does not land on 446 either) | LPF > 70 dB; metal can over the module |
| Phone display/CPU broadband noise, Qi coil | near-field pickup by the pack antenna and PCB traces | full ground plane facing the phone, shield cans, antenna on the far edge |
| Pack buck converters (750 kHz, 2.5 MHz) | harmonics at 446 MHz (harmonic #595 / #178) and conducted ripple into the module supply | forced PWM, tiny switch loops, LC post filter, ferrite bead + 100 uF at the module, spread spectrum off during RX |
| USB 12 MHz data, 48 MHz MCU, 168 MHz core | harmonics; 446 MHz = 37th of 12 MHz, close to 9th of 48 MHz (432) and 3rd of 168 MHz (504)... | pick MCU clock so no harmonic lands in 446.0-446.2 MHz (168 MHz: 2.65th, fine; USB 12 MHz 37th = 444 MHz, within 2 MHz: keep D+/D- short, CM choke, ground stitching); listen for birdies in test T-R6 and move the PLL if needed |
| Pack TX 1 W into the phone | phone's cellular/GNSS receivers blocked | 2nd harmonic at 892 MHz sits in the GSM-900 uplink (not the phone's RX); 4th at 1784 MHz is inside LTE band 3 uplink (also not the phone's RX). The LPF keeps them < -60 dBm. GNSS 1575 MHz is 3.53x, no harmonic. PMR446 TX at 1 W is comparable to the phone's own TX; expect no issue but test T-R8 |

Shielding: the SA818 comes in a tin can. Add a second can over the LPF/BPF/match
area and a ground-stitched fence between the power section and the RF area.
The back cover that faces the phone carries a ground-connected copper layer
(inner PCB layer) to form a near-complete Faraday box, with only the antenna
feed leaving it.

## 4.6 Power and digital filtering on the RF side

- +4V0 to module: 1 uH + 22 uF LC, then 600 ohm @ 100 MHz ferrite bead +
  100 uF + 100 nF + 1 nF at the VCC pin.
- PTT, PD, H/L, SQ, UART: 1 kOhm series + 100 pF to ground at the module
  (low-pass, keeps the 446 MHz field off the MCU and MCU noise off the module).
- MIC_IN: 100 ohm + 1 nF RF bypass right at the pin; AF_OUT: 1 nF bypass.
- USB D+/D-: common-mode choke, 0.5 pF TVS, 22 ohm series.
- VBUS: pi filter 10 uF / 1 uH / 10 uF, 2 kV-rated ferrite bead on the shell.
