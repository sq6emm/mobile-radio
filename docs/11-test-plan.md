# 11. Test plan

Bring-up order first, then conformance-style tests. Test IDs refer to the
requirement they cover.

## 11.1 Bring-up order

1. **Power only, no cells**: feed 7.4 V from a bench supply with 1 A limit on
   the cell pads; check rails (+4V0, +3V3D, +3V3A), ripple, sequencing. Then
   attach cells through the protector; verify charge/discharge FET control.
2. **PD controller**: program TPS25751 configuration, attach a PD analyser
   (e.g. a Type-C sniffer such as a Twinkie/PD analyser) and a laptop: observe
   contracts, Try.SRC behaviour, swaps.
3. **MCU and USB**: enumerate the vendor interface on Linux, run
   `python3 tools/mrp.py --usb info`.
4. **Radio module**: `AT+DMOCONNECT` over the debug console pass-through, then
   `tools/mrp.py channel 1`; confirm RX on a service monitor through the RF
   test port.
5. **Audio**: RX tone from the monitor to the phone/PC; TX tone from the PC
   through the module into the monitor; set levels (docs/07).
6. **Filters**: VNA sweep of the LPF and match through the test port and a
   temporary U.FL on the antenna side before the antenna is fitted.
7. **Antenna**: tune the helix on the assembled pack on a phone; measure ERP
   on an OATS or in a chamber by substitution.
8. **Phone**: Android app on three phones (recent Pixel, Samsung, one
   mid-range), iOS with the UAC profile.

## 11.2 Radio tests (conducted at the RF test port unless stated)

| ID | Test | Method | Limit / expectation |
|----|------|--------|---------------------|
| T-R1 | Channel frequencies | counter on each of the 16 channels | +/- 1.5 kHz (EN 300 296 for 12.5 kHz), expect < 300 Hz |
| T-R2a | Conducted power H/L | power meter, all channels, 6.0/7.4/8.4 V battery, -10/25/55 C | per module spec; H ~ +30 dBm, L ~ +27 dBm |
| T-R2b | ERP | substitution on OATS, pack on phone, 3 orientations | <= 27 dBm ERP with the selected level; record antenna gain |
| T-R5 | Spurious emissions TX | spectrum analyser 9 kHz-4 GHz with notch at the carrier | <= -36 dBm < 1 GHz, <= -30 dBm > 1 GHz; expect 2nd harmonic < -60 dBm |
| T-R5b | Adjacent channel power | ACP at +/-12.5 kHz with 1 kHz tone at 60 % and max deviation | <= -60 dBc or 0.2 uW |
| T-R5c | Max deviation | 1 kHz tone at +20 dB over nominal input | <= +/- 2.5 kHz (limiter proof) |
| T-R6 | Receiver sensitivity | 12 dB SINAD at the port, each channel | <= -116 dBm; also with the phone attached, screen on, data transfer running |
| T-R6b | Self-generated spurs | sweep channels, listen with squelch 0 while USB streaming, charging, sourcing | no birdie above -120 dBm equivalent on any channel |
| T-R7 | Desense by the phone | phone forced to max cellular TX (test mode or weak-signal box) on B20/B28/B8/B3/n78; measure sensitivity | <= 6 dB degradation with option A; decide option B |
| T-R8 | Pack TX into phone | pack at 1 W while the phone runs a GNSS fix and a data throughput test | no GNSS loss, < 10 % throughput drop |
| T-R9 | Blocking / spurious response | EN 300 296 methods | per standard |
| T-R10 | Busy-channel lockout, TOT, keep-alive | functional with a second radio | 180 s TOT, 2 s keep-alive, lockout refuses PTT |

## 11.3 Power and USB-C tests

| ID | Test | Expectation |
|----|------|-------------|
| T-P2 | Source PDOs with an e-load through a PD tester: 5 V/3 A, 9 V/2 A for 1 h | voltage within PD limits, shell < 45 C |
| T-P3 | Sink from 5 V/1.5 A legacy, 9 V/3 A and 15 V/2 A PD chargers | 2 A charge current, correct termination, timer |
| T-P4 | Radio rail transient when keying at 6.0 V battery | +4V0 dip < 100 mV, no module reset, no audio click > -40 dBFS |
| T-P4b | Radio rail ripple in RX | < 10 mVpp at the module, no 2.5 MHz products in the audio |
| T-P5 | Reserve behaviour | drain to 20 %: source stops, PR_Swap requested, pack trickle-sinks 100 mA; charge to 30 %: source resumes |
| T-P8 | Faults | VBUS short while sourcing; 20 V applied while sourcing; CC shorted to VBUS; reverse current | no damage, recovery |
| T-P9 | Protector | OV, UV, OC, short, over-temperature trip points | per docs/03 |
| T-PD1 | Role negotiation matrix | 6 phones x {Try.SRC win, Try.SNK win}, 3 chargers, 2 PCs, legacy A-to-C | goal state Source+UFP on every PD phone; sink-only on chargers; device-only on legacy |
| T-PD2 | Phone user enables reverse charging | pack rejects while sourcing, accepts when below reserve |
| T-PD3 | Hot plug 100 cycles | no stuck state; watchdog never fires |

## 11.4 Interface, audio and app tests

| ID | Test | Expectation |
|----|------|-------------|
| T-U1 | Enumeration on Android 12-15, Linux, Windows 11 (WinUSB), macOS | vendor interface claimable without driver install |
| T-U2 | End-to-end latency | mic tone edge at the phone to RF demodulated edge: < 60 ms; reverse likewise |
| T-U2b | Jitter / loss | 1 h stream, count `seq` gaps | < 1 gap per hour |
| T-U3 | UAC profile | iOS sees a headset, voice both ways, pack PTT works |
| T-U4 | STATUS content | compare with bench meters: battery +/- 50 mV, current +/- 100 mA, SoC +/- 5 % |
| T-A1 | Audio frequency response TX and RX | 300 Hz-3 kHz within +1/-3 dB after emphasis |
| T-A2 | Standalone mode | PTT button, speaker, mic with no phone; CTCSS decode with a consumer PMR446 radio |

## 11.5 Mechanical, thermal, environmental

| ID | Test | Expectation |
|----|------|-------------|
| T-M1 | Magnet hold | shear and pull force on an iPhone back and on a Qi2 case; 1000 attach cycles |
| T-M2 | Antenna boot flex | 10 000 cycles of 90 degree bend; VSWR change < 0.3 |
| T-M3 | Thermal map at 18 W + 10 % TX at 25 C and 35 C ambient | surface < 45 C / < 50 C |
| T-M4 | Drop 1 m, 6 faces, on a phone | no cell damage, RF performance unchanged |
| T-M5 | IP52 | per IEC 60529 |
| T-E1 | EMC pre-scan radiated emissions 30 MHz-6 GHz while sourcing and receiving | EN 301 489-1 class B margin > 6 dB |
| T-E2 | ESD 8 kV contact on antenna boot, USB shell, buttons | no reset of the radio path, no damage |
