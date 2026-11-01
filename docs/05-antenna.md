# 5. Antenna

Constraints: PMR446 requires an integral antenna (no user connector), ERP
<= 500 mW, vertical polarisation in use (M2), and the antenna sits on a pack
that is stuck to a phone with a magnet ring. The same helix also serves
the 70 cm amateur mode (430-440 MHz), so it is tuned to **439 MHz**, the
geometric centre of 432.5 and 446.1 MHz. It is the only antenna in the
product. Numbers below come from
`hardware/rf/antenna_calc.py report` (output saved in
`hardware/rf/results/antenna_report.txt`).

Wavelength at 439 MHz: 683 mm; lambda/4 = 171 mm. Nothing phone-sized is
resonant without loading, so every option is electrically short.

## 5.1 Options

| Option | Description | Expected gain | Pros | Cons |
|--------|-------------|---------------|------|------|
| **A (v1 baseline)** | Fixed stubby normal-mode helix, 60 mm long, 6 mm diameter, ~16 turns, in a TPU boot at the pack's top-left corner, feed through the PCB top edge | -3 to -6 dBd | Vertical, well understood, least sensitive to the phone body, easy to retune by trimming | Sticks out 60 mm; mechanical stress at the base |
| B (phase 2) | Meandered inverted-F printed along the top 12 mm of the PCB, 60 x 10 mm keep-out, ~151 mm conductor, 10 meanders at 6 mm pitch | -4 to -8 dBd on the phone | Nothing protrudes | Detuned by the phone body and hand (10-20 MHz shifts), needs a tuner network per phone size; efficiency maybe 20-30 % |
| C | 90 mm fold-out whip (PMR446 handheld style) | -1 to -3 dBd | Best range | Fragile on a phone-back product, still must be non-removable |

Decision: A for v1 with footprints for B's matching network so that the
same PCB can be evaluated with an internal antenna.

Mechanical placement (see docs/09): top-left corner, 25 mm from the magnet
ring edge, counterpoise = the pack's PCB ground plane (155 x 70 mm, i.e. about
lambda/4 long, which helps: the chassis is half of the radiator). The helix
axis stays vertical when the phone is portrait; in landscape the polarisation
loss to other handhelds is 10-20 dB, same as any handheld laid down.

## 5.2 Helix starting dimensions (439 MHz)

| Diameter | Axial length | Wire length | Pitch | Turns |
|----------|--------------|-------------|-------|-------|
| 6 mm | 60 mm | ~307 mm (0.45 lambda) | 3.75 mm | 16.0 |
| 8 mm | 50 mm | ~307 mm | 4.1 mm | 12.1 |
| 5 mm | 80 mm | ~307 mm | 4.2 mm | 18.9 |

Bandwidth: a 60 mm helix on this ground plane has a VSWR < 2 bandwidth of
roughly 15-20 MHz. Centred at 439 MHz that covers 432-446 MHz at VSWR <= 2
(about 0.5 dB mismatch loss at either edge). If the measured bandwidth is
narrower, the compromise is to favour 446 (the certified mode) and accept
~1 dB at 432.5 MHz; a longer (80 mm) or thicker helix widens the band.

Wire: 1.0 mm phosphor bronze or stainless spring wire on a nylon former,
soldered to a brass base insert that is screwed and soldered to the PCB. The
k = 0.45 wire-length factor is a starting point; the sleeve dielectric and the
pack shift resonance by typically -5 to -10 %, so wind 10 % longer and trim on
a VNA with the pack assembled, cover on, on a phone.

Feed impedance of such a stub on a small ground is 15-30 ohm with some
reactance. The matching footprint is a pi: shunt L (also the DC/ESD path),
series C, shunt C. Example from the calculator for a measured 22 - j15 ohm:
series 14.2 nH + shunt 8.1 pF, or series 36 pF + shunt 15.8 nH.
Use the low-pass form (series L, shunt C) because it adds harmonic rejection.

## 5.3 Internal IFA (option B) design notes

- Keep-out: no copper on any layer under the element; ground plane edge is the
  element's ground reference; feed via 50 ohm CPW from the match network.
- Shorting pin 4-6 mm from the feed; tune resonance with the meander length
  (longer = lower) and the impedance with the pin distance.
- Simulate in openEMS or NEC with the phone modelled as a 150 x 72 x 8 mm
  metal box 6 mm behind the pack, hand as a lossy dielectric block; expect
  -4 to -8 dBd realised gain.
- The magnet ring (steel/NdFeB, ~56 mm diameter) is a large conductor in the
  near field: keep the element >= 20 mm away from it.

## 5.4 ERP budget and power level selection

ERP = P_conducted - front-end loss + antenna gain (dBd). Front-end loss is
0.8 dB (LPF) + 0.3 dB (traces, ESD, test switch) = 1.1 dB.

| Module power | Antenna gain -2 dBd | -4 dBd | -6 dBd |
|--------------|---------------------|--------|--------|
| 1.0 W (H) | 0.49 W ERP, at the limit | 0.31 W | 0.19 W |
| 0.5 W (L) | 0.24 W | 0.15 W | 0.10 W |

The production test measures ERP (or antenna gain by substitution) and
stores the allowed power level per unit (`ANTENNA_GAIN_DBD_X10` in
`app_main.c`, later a calibration record). In PMR446 mode the firmware
refuses the H setting when the stored gain would put ERP above 26.5 dBm
(0.45 W, 0.5 dB margin for temperature and supply); in HAM mode 1 W is always
allowed (`bandplan_high_power_allowed`). R2 is thereby met by construction.


## 5.5 Range expectation

ERP 0.3 W, receive gain -4 dBi, sensitivity -118 dBm at the port, 10 dB fade
margin, 3 dB body loss each end: allowed path loss 125 dB.

| Environment | Path-loss exponent | Range |
|-------------|-------------------|-------|
| Line of sight (hill to hill, water) | 2.0 | tens of km (limited by horizon) |
| Open rural, ground reflections | 2.7 | ~5 km |
| Suburban | 3.2 | ~1.3 km |
| Dense urban | 3.8 | ~400 m |
| Indoors through floors | 4.5 | ~160 m |
| Plane-earth two-ray, 1.5 m antennas | - | ~2 km |

These match what consumer PMR446 handhelds achieve, which is the goal: the
pack talks to any PMR446 radio.

## 5.6 RF exposure

A 0.5 W ERP transmitter on the back of a phone is used like a walkie-talkie
(held 5-10 cm from the mouth, or on a table with the app), not at the ear.
EN 50566 / EN 62209-2 SAR assessment is needed because the device can be
within 20 cm of the body; PMR446 handhelds at this power routinely pass. The
app instructs speaker-mode use and the firmware's 180 s time-out limits
duty cycle. Keep the antenna at the top edge so it is as far as possible from
the hand holding the phone.
