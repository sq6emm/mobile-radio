# 10. Regulatory and safety

This product combines a licence-free transmitter with a lithium battery
power bank. Both halves carry their own obligations; the radio half dominates
the design constraints.

## 10.1 PMR446 rules (CEPT / EU)

Basis: ECC Decision (15)05 (as amended), ERC Recommendation 70-03 Annex 2,
harmonised standard ETSI EN 300 296 (analogue), EN 300 113 (digital, not used).

| Rule | Consequence for this design |
|------|-----------------------------|
| 446.0-446.2 MHz, 16 analogue channels at 12.5 kHz | channel table in `pmr446.c`, firmware can not tune elsewhere |
| Max 500 mW ERP | antenna gain measured per unit, module power level capped (docs/05) |
| Integral antenna only | fixed helix, no external connector; the RF test switch is internal |
| Hand-portable use only, no base stations, repeaters, or fixed installations | the product is a phone accessory; the app does not offer any relay function |
| Maximum transmit time limited by the equipment (180 s recommended) | `ptt_guard` TOT 180 s |
| Voice; data allowed in several countries with limits on duty cycle | DATA_TX is a phase-2 feature behind a region flag |
| No encryption of the voice content | none used; CTCSS/DCS are squelch tones, not privacy |
| Country variations (some non-EU CEPT members restrict channels or require registration) | region table in firmware; only EU is enabled; the app shows the country list |

Outside CEPT the band is not licence-free (in the US 446 MHz is amateur
radio). A US variant would be an FRS device under 47 CFR Part 95 (462/467 MHz,
2 W, fixed antenna) and needs separate FCC certification; the module covers
the frequency range, so it is a firmware region plus a certification, not a
redesign.

## 10.2 CE marking (RED 2014/53/EU) and applicable standards

| Area | Standard | Notes |
|------|----------|-------|
| Radio spectrum (art. 3.2) | EN 300 296-1/-2 | TX power, spurious, adjacent channel power, frequency error, RX sensitivity, blocking, spurious response, with the phone attached as the "host" |
| EMC (art. 3.1b) | EN 301 489-1, EN 301 489-5 | radiated/conducted emissions and immunity, also as a USB-C PD source |
| Safety (art. 3.1a) | EN IEC 62368-1 | audio/video and ICT equipment safety; covers the power bank function, touch temperature, battery enclosure |
| RF exposure (art. 3.1a) | EN 50566, EN 62209-2 | SAR for body-worn/hand-held use at < 20 cm; 0.5 W ERP at 446 MHz |
| Battery | EN IEC 62133-2 | cells and pack |
| Transport | UN 38.3 | mandatory before shipping; 29.6 Wh < 100 Wh (no dangerous goods class for carry-on) |
| Environmental | RoHS, REACH, WEEE, Batteries Regulation (EU) 2023/1542 (removability requirement from 2027 for portable batteries: design the cell pack to be replaceable with common tools) | |
| USB | USB-IF compliance is voluntary; without certification the USB logos can not be used, but PD interoperability testing is still required by the product (docs/11) | |

Conformity route: EN 300 296 is a harmonised standard, so self-declaration
with a test report from an accredited lab is possible; many manufacturers still
use a Notified Body for the radio part. Budget 4-6 weeks and test samples with
the RF test port accessible.

## 10.3 Battery and power-bank safety requirements folded into the design

- Dual protection (primary IC + secondary OVP with fuse), NTC monitored
  charging window 0-45 C, discharge window -20 to 60 C.
- Cells rated for the 2 C peak (sourcing 18 W + TX = ~3.5 A peak at 6 V).
- VBUS source: current limit, OVP, short-circuit shutdown, reverse-current
  blocking; no VBUS unless a sink is detected (Type-C rule, prevents live
  pins).
- Enclosure: no cell deformation under 1 m drop; vent path for the pouch
  cells; pouch tabs insulated; no sharp PCB edges facing the cells.
- Labelling: Wh rating, polarity, CE, WEEE, "do not short", manufacturer,
  model, serial, PMR446 statement.

## 10.4 User-facing restrictions the app enforces or explains

- Country selector limited to countries where PMR446 is permitted; first run
  shows the licence-free conditions.
- Speaker-mode PTT only (no "hold at ear" call UI) to keep the RF exposure
  posture.
- Warning when the phone transmits heavily (hotspot on, poor cellular signal):
  radio range may drop.
