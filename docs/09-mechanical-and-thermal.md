# 9. Mechanical, magnetic attachment and thermal

## 9.1 Form factor

| Item | Value |
|------|-------|
| Body | 160 x 76 x 14 mm, PC/ABS shell, TPU bumper edge |
| Antenna | 60 mm stub at the top-left corner, 8 mm boot diameter, flexible TPU over the helix |
| Mass | ~190 g (cells 2 x 65 g, PCB/shell/magnets ~60 g) |
| Attachment | magnet ring to the phone back (9.2) + 100 mm USB-C to USB-C cable (USB 2.0, 3 A) from the pack's PHONE port to the phone's port; cable clips into a groove along the pack edge |
| Controls | PTT button (side, 12 x 6 mm, tactile under TPU), power/pair button, 2 LEDs (battery, TX/RX), speaker grille, mic port |
| Ports | two USB-C receptacles on the bottom edge, 22 mm apart: **PHONE** (right of centre, cable runs straight down to the phone's port) and **CHARGE** (left), both labelled in the moulding and by LED colour; the charger cable hangs below the phone while in use |

## 9.2 Magnetic attachment: Qi2 MPP / MagSafe-compatible ring

The pack glues itself to the phone with the standard magnet ring used by
MagSafe and by the Wireless Power Consortium's Qi2 Magnetic Power Profile
(MPP). Any phone with the ring built in (iPhone 12 and later, Qi2 Android
phones) or any case with a ring snaps onto it with the correct alignment and
orientation.

Geometry (follow the published Qi2 MPP / Apple accessory design guideline
drawings when ordering; numbers here are the approximate envelope):

| Parameter | Value |
|-----------|-------|
| Ring magnet array | ring of arc-segment NdFeB N52 magnets, outer diameter ~56 mm, inner diameter ~46 mm, thickness 1.0-1.5 mm, alternating-polarity pairs around the ring as in the standard |
| Orientation magnet | one bar magnet below the ring (the standard's "orientation" feature) so the pack always mounts upright: this keeps the antenna vertical and the cable pointing at the port |
| Position on the pack | ring centre on the pack's vertical centre line, 80 mm from the top edge (phone coil centres sit near the middle of the phone back; with a 160 mm pack on a 150-165 mm phone this lines up within a few mm) |
| Hold | > 8 N shear on an iPhone back glass, i.e. it survives a pocket but detaches with a twist; a thin silicone ring on the pack face raises friction |
| Steel shunt | a 0.3 mm steel backing behind the array (as in commercial rings) roughly doubles the holding force and reduces field inside the pack |

Interactions inside the pack:

- The ring is a large conductive loop. Keep the antenna feed and the helix
  base >= 20 mm from it (the top-left corner placement gives 45 mm) and keep
  the match network outside its footprint.
- No wireless charging coil in v1 (out of scope), so the ring's interior is
  free for the cells. If Qi2 charging is added later the coil sits inside the
  ring and needs its own ferrite; the receiver would then need a Qi coil
  switching-noise study (110-205 kHz, harmonics negligible at 446 MHz but the
  coil is a loop antenna for digital noise).
- The phone's compass/magnetometer sees the ring; this is the same as any
  MagSafe accessory and the phone compensates.
- The speaker's own magnet must sit outside the ring so it does not disturb
  the ring's polarity pattern.
- Phones without a built-in ring: ship an adhesive ring (standard
  accessory) or a thin case with one.

Alternative kept in reserve: a snap-on case frame that holds the pack without
magnets, for phones that must not have magnets on them (medical devices
nearby).

## 9.3 Stack-up (back of phone -> outside)

```
phone back glass
silicone friction ring / pack face (1.0 mm shell with inner copper foil ground)
magnet ring (1.2 mm) + steel shunt (0.3 mm), potted in the shell face
cells 2 x 60 x 70 x 6 mm, side by side, foam pads
PCB 4-layer 1.2 mm, 150 x 68 mm, components facing outwards
  RF area (top-left 40 x 30 mm) with shield can 4 mm high
  power area (bottom, between the two USB-C ports: TPS25751, STUSB4500, BQ25798, TPS55288) with shield can
  MCU/audio middle
outer shell 1.0 mm with speaker grille and buttons
```

PCB 4-layer: L1 components and RF, L2 solid ground, L3 power, L4 ground /
signals. RF CPW on L1 referenced to L2. Thermal vias under the module's PA
pad and under the charger.

## 9.4 Thermal

| Source | Dissipation | Condition |
|--------|-------------|-----------|
| SA818 at 1 W RF | ~2 W (3.0 W in, 1 W out) | TX, 10 % duty -> 0.2 W average; 180 s continuous allowed |
| BQ25798 charging at 20 W from a 45 W charger | ~1.0 W | pass-through |
| TPS55288 sourcing 18 W to the phone | ~1.1 W | continuous while charging the phone |
| TPS62130 at 0.8 A | ~0.15 W | TX |
| LDOs | ~0.25 W | |
| Cells at 2.5 A discharge, 60 mOhm | ~0.4 W | sourcing |

Worst realistic case is pass-through: 45 W in, 18 W out, 20 W into the
cells, plus 10 % TX, about 3.5 W dissipated in a 160 x 76 mm pack. In free
air that is roughly 15 C above ambient at the shell; stacked on a phone one
side is insulated, so expect 18-22 C at the warmest spot over the power
section. That exceeds the 45 C target (M3) above about 25 C ambient, which is
why the firmware throttles the battery charge current at 50 C board
temperature (to 60 %) and 60 C (to 25 %) before touching the phone's supply.
Without a charger (18 W out only) the rise is 8-10 C as before. The 180 s TX time-out limits the module's spike to ~2 W x 180 s
= 360 J, heating the RF area about 10 C transiently. Firmware throttles
source current at 60 C board temperature and inhibits TX at 70 C PA
temperature.

## 9.5 Ingress and robustness

- IP52 target: gasketed shell seam, membrane over mic and speaker, USB-C
  open (rated connector), antenna boot bonded.
- Antenna base: brass insert with M3 thread into a boss, PCB soldered to a
  pin; the boot flexes 90 degrees without transferring load to the solder.
- Drop: 1 m onto concrete, pack alone and on a phone; the magnet ring
  separates before the phone port or cable takes the load.
