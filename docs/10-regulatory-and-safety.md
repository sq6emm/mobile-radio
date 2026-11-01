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

## 10.1b Amateur (70 cm) mode

The HAM mode makes the pack an amateur radio transceiver for 430-440 MHz.
Different rules apply to that mode, and the product must keep the two apart:

| Point | How the design handles it |
|-------|---------------------------|
| Only licensed operators may transmit in the amateur bands | HAM mode is locked until a callsign is stored; the app shows the licence notice; the operator is legally responsible (same as any ham transceiver with a keypad). |
| Station identification | every AX.25 / APRS / LoRa-APRS frame carries the callsign-SSID; the app reminds the user to identify by voice at the start/end of a QSO and every 10 min (national rules differ). |
| Equipment built or modified by amateurs is exempt from RED conformity assessment only for the amateur's own use; a product sold to the public is not exempt | the product is CE marked under RED for the PMR446 and ISM functions; the HAM mode is documented as an amateur-radio function of the certified product (as commercial ham transceivers do). EMC and safety assessments cover the device in all modes. |
| Band plan (IARU Region 1, 70 cm) | 432.500 MHz APRS, 433.500 MHz FM calling, 433.775 MHz LoRa-APRS, 438.025 repeater outputs etc.; the app shows the band plan; the firmware only enforces the band edges and the 12.5 kHz grid. |
| Power | 1 W conducted is well under any amateur limit; no ERP cap in HAM mode. |
| Third-party traffic, encryption | no encryption of content anywhere in the product; messages are plain APRS. |
| PMR446 interoperation | a ham may not use the PMR446 channels with more power or an external antenna than PMR446 rules allow: PMR mode keeps its caps regardless of the unlock. |

## 10.1c LoRa under EN 300 220 (short-range devices)

| Band | Limit | Firmware enforcement |
|------|-------|----------------------|
| 433.050-434.790 MHz (SRD, Annex B band h1.3 class) | 10 mW ERP, no duty limit (h1.3: 10 mW, 100 % duty with <= 25 kHz channel... LoRa 125 kHz falls under the 10 mW / 10 % duty cycle class h1.1-h1.2 in some readings: use **10 mW ERP and 10 % duty** to be safe) | PMR/ISM mode caps 433 LoRa at +10 dBm minus antenna gain, 10 % duty; HAM mode lifts both |
| 869.400-869.650 MHz (band h1.6 class) | 500 mW ERP, <= 10 % duty cycle | time-on-air accounting per hour, TX refused above the quota |
| 868.000-868.600 MHz (h1.4) | 25 mW ERP, <= 1 % duty | same, if the user tunes there |
| 863-870 MHz general | 25 mW ERP, 0.1 % or LBT+AFA | not offered as a preset |

The LoRa modules themselves must be CE/RED assessed with the product (their
own module certificates help with the radio test report but do not replace
the product-level assessment).

## 10.1d Digital voice and data

- FreeDV 2400B is an open amateur mode (Codec2, LGPL 2.1): no licence
  issue, HAM mode only.
- Codec2's LGPL requires that users can relink the firmware with a modified
  library: ship the firmware as object files plus a documented link step, or
  provide the full source (recommended; the rest of this project is open).
- On PMR446 only analogue FM voice and simple data are offered; the digital
  PMR446 standards (dPMR446 EN 301 166, DMR446 EN 300 113) are not claimed.

## 10.2 CE marking (RED 2014/53/EU) and applicable standards

| Area | Standard | Notes |
|------|----------|-------|
| Radio spectrum (art. 3.2) | EN 300 296-1/-2 (PMR446 FM), EN 300 220-1/-2 (433 and 868 MHz LoRa as SRD), amateur mode documented | TX power, spurious, adjacent channel power, frequency error, RX sensitivity, blocking, spurious response, with the phone attached as the "host"; LoRa modules tested in the product |
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
