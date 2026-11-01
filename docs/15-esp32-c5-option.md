# 15. ESP32-C5 option: a third radio family and a wireless phone link

Suggestion under review: use the ESP32-C5, which can drive its own radios as
a raw transceiver on 2.4 GHz and 5 GHz (raw 802.11 frame injection and
promiscuous receive, the Espressif 802.11 LR long-range PHY, BLE 5 coded PHY,
IEEE 802.15.4). This chapter evaluates where it fits. Numbers for the C5 are
from the Espressif datasheet and SDK documentation and should be re-checked
against the current revision (**VERIFY**).

## 15.1 What the ESP32-C5 is

| Item | ESP32-C5 (WROOM-1 module) |
|------|---------------------------|
| CPU | single-core RISC-V, 240 MHz, 384 KB SRAM (+ optional PSRAM), low-power core |
| Radios | Wi-Fi 6 dual band 2.4 / 5 GHz (20 MHz), Bluetooth 5 LE (incl. coded long-range PHY), IEEE 802.15.4 (Zigbee / Thread) |
| Raw radio access | `esp_wifi_80211_tx` raw frame injection, promiscuous RX, ESP-NOW (connectionless 802.11 data frames), 802.11 LR proprietary PHY (2.4 GHz only, 250 kbit/s class, ~ -105 dBm sensitivity, about 1 km line of sight), BLE advertising/GATT, 802.15.4 raw frames |
| TX power | up to about +20 dBm at 2.4 GHz, less at 5 GHz; the module is pre-certified (CE/FCC) with its PCB antenna |
| Peripherals | USB Serial/JTAG (a fixed CDC device, **not** a USB OTG controller), 12-bit ADC, no DAC, I2S, SPI, UART, SDIO slave, TWAI |
| Supply | 3.3 V, 100-350 mA bursts during Wi-Fi TX, < 100 uA deep sleep |

What it cannot do: anything below 2.4 GHz. PMR446, the 70 cm band and the
433/868 MHz LoRa bands stay with the SA818 and the SX1262s. The published
hacks that make ESP32 chips radiate at arbitrary sub-GHz frequencies through
clock-output harmonics are not usable in a product (broadband spurious
emissions, no filtering, illegal).

## 15.2 Three ways to use it

### Option A: ESP32-C5 as the main MCU (replaces the STM32F405)

| For | Against |
|-----|---------|
| One chip does control, audio DSP, Wi-Fi/BLE to the phone, pack-to-pack 2.4/5 GHz | **No USB OTG device**: it cannot present the vendor MRP interface, the UAC sound-card profile or a CDC-with-RTS PTT. The whole USB phone link and the Digirig compatibility would be lost; the phone link would have to be BLE/Wi-Fi only |
| Cheaper than STM32F405 + nothing else | No DAC (external I2S codec needed), single core shared with the Wi-Fi/BLE stacks (Codec2 + modem + stacks is tight), radio firmware is closed binary |
| Pre-certified module simplifies the radio part of RED for 2.4/5 GHz | A Wi-Fi transmitter in the pack means another coexistence study next to a -120 dBm receiver (out of band, so manageable, but it adds test time) |

Verdict: not for this product as specified. It would be the right core for a
*different* product, a wireless-only accessory with USB used for power alone.

### Option B: ESP32-C5 as a connectivity co-processor next to the STM32F405 (recommended)

The STM32 keeps USB, audio, the UHF radios and the interlocks. The C5 sits on
a UART (2 Mbit/s, with flow control) or SDIO and offers:

| Function | How | What it buys |
|----------|-----|--------------|
| **Wireless phone link** | MRP frames over BLE GATT (control, status, messages) and over Wi-Fi UDP (PCM audio, MRP framing unchanged) in SoftAP or station mode | The phone no longer needs the cable for data: the pack can sit in a bag or on a window while the phone is in hand. **iOS gets full app support** without MFi, because BLE and Wi-Fi are open to iOS apps while USB vendor interfaces are not |
| **Pack-to-pack 2.4 GHz long range** | ESP-NOW over the 802.11 LR PHY, raw frames with our own header; MRP messages, positions, even voice (Codec2 1300 needs 1.3 kbit/s) | ~1 km line of sight, licence-free, higher throughput than LoRa, no duty-cycle rule beyond EN 300 328 |
| **Pack-to-pack 5 GHz** | ESP-NOW / raw frames at 5 GHz | Higher data rate for short range (files, images). In the EU outdoor use is confined to 5.470-5.725 GHz with DFS/TPC or 5.725-5.875 GHz at 25 mW SRD; 5.15-5.25 GHz is indoor only. Useful, but 2.4 GHz LR is the long-range tool |
| BLE coded PHY | advertising beacons with position | A low-power "where is my group" beacon for other phones even without our app running |
| 802.15.4 / Thread | optional | sensor or home-automation tie-in; not a goal here |
| Over-the-air update | Wi-Fi | firmware updates for both MCUs without a PC |

Costs: one ESP32-C5-WROOM-1 module (about 3-4 EUR), its own 3.3 V budget
(peak 350 mA, shares +3V3R with the LoRa modules only if the regulator grows to
1 A), a dual-band 2.4/5 GHz PCB or chip antenna with keep-out at a third
corner (or the module's built-in antenna, which keeps its certification),
one UART, one GPIO for enable/boot, 20 x 25 mm of board, and the coexistence
tests T-C1..T-C3 below.

### Option C: later add-on

Keep the footprint and the UART on the board, populate the module in a second
build. Costs almost nothing now and keeps the decision open. This is the
minimum I would do.

## 15.3 Recommendation

Option B, in two steps: lay out the board for the module (Option C) and
bring up the wireless phone link first, because it changes the user
experience most (no cable for data, iOS support). Pack-to-pack 2.4 GHz LR
comes second; 5 GHz raw frames last, as a short-range extra.

The protocol work is small because MRP is transport agnostic: the same frames
travel over USB bulk, BLE (a GATT characteristic carrying frames with
notification), Wi-Fi UDP (one or more frames per datagram) and the
STM32-to-C5 UART. The C5 firmware is a bridge plus the ESP-NOW/LR pack-to-pack
layer; it does not touch the radios' safety interlocks, which stay in the
STM32.

## 15.4 Design impact if Option B is adopted

| Area | Change |
|------|--------|
| Architecture (docs/02) | new block "ESP32-C5 connectivity", UART2 2 Mbit/s with RTS/CTS, EN and BOOT GPIOs, +3V3W rail (1 A) |
| Power (docs/02, 03) | +0.1 W average with BLE on and Wi-Fi off; +0.5-1 W while streaming audio over Wi-Fi; deep sleep when the cable is attached (USB link preferred) |
| RF (docs/04) | third antenna at the bottom-left corner (the magnet ring and the USB port constrain placement); 2.4 GHz TX is 5.4 x 446 MHz: no harmonic relation; the FM LPF/BPF give > 70 dB; the LoRa 868 front end needs the module's own 2.4 GHz rejection (SX1262 input filter) checked in T-C2 |
| Protocol (docs/06) | MRP over BLE: service UUID, one write characteristic (host to pack) and one notify characteristic (pack to host), 244-byte MTU fragments; MRP over UDP: port, discovery via mDNS; same message set |
| Firmware (docs/08) | `link_mux`: the STM32 accepts MRP from USB, UART-from-C5 (BLE/Wi-Fi) with a single active "voice host" at a time; C5 firmware in `firmware/esp32c5/` (ESP-IDF) |
| Regulatory (docs/10) | EN 300 328 (2.4 GHz), EN 301 893 (5 GHz), covered mostly by the module's certification; product-level EMC still required |
| Test plan (docs/11) | T-C1 Wi-Fi TX vs FM/LoRa receiver desense; T-C2 LoRa 868 blocking by 2.4 GHz; T-C3 BLE audio-control latency; T-C4 ESP-NOW LR range on the pack antenna |
| Mechanical (docs/09) | module placement away from the magnet ring and the hand |

## 15.5 Open questions for review

1. Is the wireless phone link (BLE + Wi-Fi) wanted for v1, or is USB-only
   acceptable for the first board (Option C now, B later)?
2. Pack-to-pack at 2.4 GHz LR vs LoRa 868: both? LR is faster and licence
   friendly; LoRa 868 reaches further through walls. If one has to go, which?
3. Is 5 GHz worth the antenna complexity given the EU outdoor restrictions?
4. Should the C5 own the BLE "find my group" beacon even when the pack is
   otherwise asleep?
