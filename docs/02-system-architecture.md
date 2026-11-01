# 2. System architecture

## 2.1 Block diagram

```mermaid
flowchart LR
    subgraph PHONE[Phone]
        APP[App: PTT, channel, status]
    end

    subgraph PACK[Battery pack]
        direction TB
        USBC[USB-C PHONE port<br/>VBUS, CC1/2, D+/D-]
        USBB[USB-C CHARGE port<br/>VBUS, CC1/2]
        PD[TPS25751<br/>PD controller, PHONE]
        PDB[STUSB4500<br/>PD sink, CHARGE]
        SRC[TPS55288<br/>buck-boost 5/9/12 V<br/>to PHONE port]
        CHG[BQ25798<br/>charger, inputs VAC1 PHONE<br/>VAC2 CHARGE]
        BAT[2S Li-ion 7.4 V 4 Ah<br/>protector + BQ34Z100 gauge]
        BUCK[TPS62130<br/>4.2 V / 3 A radio rail]
        LDO[3.3 V LDOs<br/>digital + analog]
        MCU[STM32F405<br/>USB FS device: MRP / CDC / UAC<br/>48 kHz ADC/DAC audio, UART, I2C<br/>AFSK, AX.25, Codec2/FreeDV]
        RADIO[SA818S-CE module<br/>430-446 MHz FM, 1 W / 0.5 W]
        LPF[7-pole Chebyshev LPF<br/>fc 500 MHz]
        BPF[2-pole BPF 439 MHz<br/>50 MHz wide]
        MATCH[Match + ESD<br/>+ RF test switch]
        ANT((Integral helix<br/>439 MHz))
        AUDIO[Audio front end<br/>anti-alias, buffers,<br/>speaker amp, mic]
        UI[PTT button, LEDs]
    end

    APP <-- "USB 2.0 FS, MRP protocol" --> USBC
    USBC <-- "VBUS 5/9 V, CC" --> PD
    USBC -- "D+/D-" --> MCU
    PD <-- I2C --> MCU
    PD -- "VBUS in (VAC1)" --> CHG
    SRC -- "VBUS out" --> PD
    CHG -- VSYS --> SRC
    USBB -- "VBUS 5-20 V" --> PDB
    PDB -- "VAC2" --> CHG
    PDB <-- I2C --> MCU
    CHG <--> BAT
    CHG -- VSYS 6-8.4 V --> BUCK
    CHG -- VSYS --> LDO
    BUCK -- 4.2 V --> RADIO
    LDO --> MCU
    LDO --> AUDIO
    MCU -- "UART 9600, PTT, PD, H/L, SQ" --> RADIO
    MCU <-- "ADC / DAC" --> AUDIO
    AUDIO <-- "AF_OUT / MIC_IN" --> RADIO
    RADIO -- ANT --> LPF --> BPF --> MATCH --> ANT
    UI --> MCU
    CHG <-- I2C --> MCU
    BAT <-- I2C --> MCU
```

## 2.2 Design decisions

| Decision | Choice | Why | Rejected alternatives |
|----------|--------|-----|-----------------------|
| FM radio | NiceRF SA818S-CE (UHF, 400-480 MHz) | The current, CE-RED certified revision of the SA818 family: same pins and commands, LNA (+4 dB sensitivity), in stock at distributors (docs/04). Covers PMR446 and the 70 cm amateur band. | SA818 V4 (older, no LNA), DRA818U (Dorji clone), SA868S 2 W (power not usable in PMR), SA828 (all-in-one with speaker amp; fewer UART options), DMR828S (digital, AMBE++, 60 USD; a possible later "digital" variant). |
| Modes | PMR446 (anyone) + HAM 430-440 MHz (unlocked by callsign) | Same hardware; firmware band plan and power rules. | Separate products. |
| Single radio | Only the 400 MHz FM module | Decision of the November 2026 review: certification, coexistence and cost; the phone provides every other link. | LoRa 433/868 (SX1262) and an ESP32-C5 connectivity co-processor were designed and then removed; the write-ups remain in the git history (commit c51054c). |
| Two USB-C ports | PHONE (DRP, data) + CHARGE (sink, power only) | Charge the pack and the phone at the same time; a BQ25798 with two inputs plus a separate TPS55288 source converter make in and out simultaneous. | One port with role swapping only (no pass-through); power-bank SoCs (SW6306 class: no control over the USB data role). |
| Digital voice | FreeDV 2400B / Codec2 1300 in firmware | Open, runs on STM32F405 (SM1000 precedent), passes through the FM audio path. HAM only. | M17 / DMR 4FSK (need a flat 9600 Bd modulator path: verify on the module first), dPMR446 (licensed AMBE). |
| Sound-card profile | UAC2 48 kHz + CDC RTS-PTT (Digirig-compatible) | Every existing digital-mode program and iOS work without our app. | Vendor-only link. |
| Antenna | Integral stubby helical (v1), PCB IFA option | Vertical, predictable, least affected by the phone body. | Internal-only IFA first (risky when stacked on a metal phone), see docs/05. |
| Filters | External 7-pole LPF in common path; BPF optional | Module harmonics alone are marginal against -36 dBm; LPF also protects the receiver from the phone's 700-900 MHz uplink. | Elliptic LPF (sharper but less tolerant); SAW in common path (cannot take 1 W). |
| MCU | STM32F405RGT6 (168 MHz, 192 KB RAM, FPU) | USB FS device, two 12-bit DACs and fast ADCs for 48 kHz audio without a codec, DMA, TinyUSB; Codec2 1300 + FreeDV 2400B are proven on this exact part (SM1000). Risk: the combined load (DV + USB audio + AFSK RX) must be profiled in Phase C; STM32F427/H743 are the drop-in escalation. | RP2040 (no DAC, no FPU), ESP32-S3 (adds a 2.4 GHz radio next to a -120 dBm receiver). |
| Phone link | Vendor bulk interface + MRP, UAC optional | Avoids Android global audio re-routing, one protocol for everything, works on Linux/Windows for bench. | UAC + HID (Android hijacks audio; iOS has no vendor access anyway); BLE (another radio, latency, pairing). |
| PD controller | TPS25751D with BQ25798 | TI reference pairing for DRP power banks, handles swaps and protections, I2C to MCU. | FUSB302B + software PD stack on the MCU (more flexible, much more firmware; kept as alternative). |
| Battery | 2S1P 4 Ah Li-ion pouch | 9/12 V source with buck-boost efficiency, thin, 29.6 Wh. | 1S (boost from 3.7 V to 9 V at 2 A is lossy, hot); 2S2P 18650 (too thick for a phone-back pack). |
| Audio | MCU ADC/DAC at 48 kHz, 8 kHz analog corners, firmware resampling to 16 kHz for voice | Voice needs only 16 kHz, but 48 kHz with a flat 6 kHz path keeps 9600 Bd modes, FreeDV (48 kHz modem) and the UAC sound-card profile possible. | 16 kHz only (cheaper filters, closes doors). |
| Magnetic attachment | Qi2 MPP / MagSafe ring with orientation magnet | Snaps upright on any ring-equipped phone or case. | Clip-on case frame (kept as alternative). |

## 2.3 Power budget

Battery 2S, 7.4 V nominal, 4.0 Ah = 29.6 Wh; usable ~27 Wh.

| Mode | Consumers | Power | Endurance (radio only) |
|------|-----------|-------|------------------------|
| Standby, FM RX, USB idle | module 60 mA @ 4.2 V (0.25 W), buck/LDO/MCU ~0.18 W, gauge/PD controllers ~0.03 W | ~0.46 W | ~59 h |
| Digital voice TX | FM TX 3.3 W + MCU load ~0.1 W | ~3.4 W | as voice |
| Pass-through: 45 W charger on CHARGE, phone taking 18 W | 18 W to the phone via TPS55288 (~94 %), ~2 W radio/logic, remainder (~22 W) into the battery at up to 2 A | losses ~3 W | battery full in ~1.5 h while the phone charges |
| RX with audio to phone | + ADC/DMA/USB ~0.1 W | ~0.55 W | ~49 h |
| TX 1 W RF (module ~750 mA @ 4.2 V) | 3.0 W + 0.3 W | ~3.3 W | 10 % duty average ~0.8 W -> ~34 h |
| Sourcing 18 W to phone | 18 W out, ~94 % buck-boost | ~19.2 W from battery | ~1.4 h, delivers ~25 Wh |
| Charging the pack at 27 W input | 2 A into cells | - | 0-80 % in ~1.7 h |

Radio keeps priority: sourcing stops at 20 % state of charge, leaving about
5.5 Wh, i.e. roughly 7 h of RX/TX use (docs/03).

## 2.4 Interfaces inside the pack

| From | To | Signal |
|------|----|--------|
| MCU USART1 | SA818 RXD/TXD | 9600 8N1 AT commands, 3.3 V logic |
| MCU GPIO | SA818 PTT (active low), PD (power down, low = sleep), H/L (power level) | open-drain or push-pull 3.3 V |
| SA818 SQ | MCU GPIO (EXTI) | squelch status, low when carrier present (VERIFY polarity on the module revision) |
| SA818 AF_OUT | buffer + 8 kHz anti-alias filter -> MCU ADC1 at 48 kHz | 0-3.3 V audio, DC biased |
| MCU DAC1 at 48 kHz | 8 kHz reconstruction filter -> attenuator -> SA818 MIC_IN | 10-100 mVpp into the module mic input (VERIFY level) |
| MCU I2C1 | TPS25751 (PHONE port), BQ25798, BQ34Z100-G1 | 400 kHz, with interrupt lines from PD and charger |
| MCU I2C1 | STUSB4500 (CHARGE port sink controller), TPS55288 (PHONE port source converter) | same bus, interrupt from STUSB4500 |
| MCU USB | PHONE port only. Profile 0: vendor MRP + CDC; profile 1: UAC2 48 kHz mono in/out + CDC (RTS = PTT) | soft re-enumeration on profile change |
| MCU USB FS | USB-C D+/D- (both pairs tied) | 12 Mbit/s |
| MCU GPIO | PTT button, 2 LEDs, speaker amp enable, mic enable | - |
| MCU ADC | PA thermistor, antenna-switch sense (test port inserted) | - |
