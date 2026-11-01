# 7. Audio path and audio filtering

Half-duplex voice, 300-3000 Hz, FM deviation +/- 2.5 kHz max at 12.5 kHz
spacing, plus the data modes that ride on the same path: 1200 Bd AFSK
(1200/2200 Hz), FreeDV 2400B (48 kHz modem audio) and, later, 9600 Bd modes.
The hardware therefore samples at **48 kHz** with analog corners at 8 kHz;
voice is decimated to 16 kHz in firmware (`resample.c`, 96-tap FIR, 0.0 dB at
1-3 kHz, > 50 dB alias rejection). Two audio sources and two sinks exist and
are switched by the PTT state, the mode and whether a phone is attached.

```
            RX                                        TX
SA818 AF_OUT -> buffer -> 2nd-order AA LPF -> ADC (16 kHz) -> MRP AUDIO_RX -> phone
             -> speaker amp (standalone)        phone -> MRP AUDIO_TX -> DAC (16 kHz)
                                                  -> reconstruction LPF -> attenuator -> SA818 MIC_IN
local MEMS mic -> preamp -> HPF -> (standalone) -------------------------------^
```

## 7.1 Receive side

1. **Module AF_OUT**: ~0.3-0.6 Vpp line level, DC biased (**VERIFY**), already
   de-emphasised and band limited (300 Hz-3 kHz) inside the module, CTCSS tone
   removed by its 300 Hz high-pass.
2. **Buffer and anti-alias filter**: TLV9062 (rail-to-rail, 3.3 V) unity
   buffer, then a 2nd-order Sallen-Key low-pass at 8 kHz (Butterworth:
   R 10 k / 10 k, C 1.4 nF / 2.8 nF), biased at 1.65 V. With 48 kHz sampling
   the alias band starts at 40 kHz where this filter gives > 28 dB.
3. **ADC**: STM32 ADC1, 12-bit, timer-triggered at exactly 48.000 kHz,
   DMA double buffer of 960 samples (20 ms). Firmware subtracts the DC bias,
   scales to s16 and either decimates to 16 kHz (voice to the phone, AFSK
   demodulator) or feeds FreeDV at 48 kHz. A 1st-order 250 Hz high-pass
   kills residual CTCSS/hum on the voice path only (data paths stay flat).
4. **Speaker (standalone)**: TPA6211A1 class-AB 3 W mono into an 8 ohm,
   28 mm speaker; class-AB rather than class-D so no switching products reach
   the receiver. Enabled only when no host is attached or the app asks.

## 7.2 Transmit side

1. **From the phone**: 20 ms PCM frames into a DAC ring buffer (3 frames deep,
   starts playing when 2 are queued).
2. **DAC**: STM32 DAC1 12-bit at 48 kHz, DMA, output 0.2-3.1 V. Voice from
   the phone (16 kHz) is interpolated x3 in firmware; AFSK and FreeDV modem
   audio are generated at 48 kHz directly.
3. **Reconstruction filter**: 2nd-order Sallen-Key 8 kHz (removes the 48 kHz
   image), then a 1st-order 300 Hz high-pass that can be bypassed by an
   analog switch (TS5A3159) for flat-path data modes.
4. **Attenuator and limiter**: resistive divider to the module's microphone
   level (tens of mV; **VERIFY** the module's MIC_IN sensitivity for 2.5 kHz
   deviation, typically 10-30 mVrms), a soft clipper in firmware before the
   DAC (peak limiter at -3 dBFS) so that deviation can never exceed the legal
   limit regardless of what the app sends. Pre-emphasis is done by the module
   (`AT+SETFILTER` bypass flags all 0).
5. **Local microphone (standalone)**: analog MEMS microphone (Knowles
   SPU0410LR5H class) with a 2 kHz-cornered noise-gate in firmware, preamp
   gain 30 dB; muxed to the same reconstruction path through the DAC (the
   MCU samples the mic on ADC2 and re-synthesises it, which also gives the
   limiter for free).

## 7.3 Filters summary

| Filter | Where | Corner / type | Purpose |
|--------|-------|---------------|---------|
| Pre-emphasis 6 dB/oct | module, TX | 300 Hz-3 kHz | FM voice SNR, standard PMR |
| De-emphasis | module, RX | | |
| CTCSS reject HPF | module, RX | 300 Hz | removes sub-audible tone |
| Voice LPF | module, both | 3 kHz | deviation / adjacent channel |
| Anti-alias LPF | hardware, RX | 8 kHz 2nd-order | ADC at 48 kHz |
| Reconstruction LPF | hardware, TX | 8 kHz 2nd-order | DAC image |
| Band HPF | hardware, TX, switchable | 300 Hz 1st-order | keep voice above CTCSS band; bypassed for data |
| Decimation / interpolation FIR | firmware | 6 kHz, 96 taps | 48 <-> 16 kHz voice path |
| DC/hum HPF | firmware, RX voice | 250 Hz 1st-order IIR | DC bias, residual tone |
| AFSK band-pass | firmware, RX data | 900-2700 Hz biquad | demodulator front end |
| Peak limiter | firmware, TX | -3 dBFS | deviation limit, R5 |
| Noise gate | firmware, local mic | -45 dBFS | standalone mic hiss |
| RF bypass | hardware | 1 nF + 100 ohm at module pins | 446 MHz on audio lines |

## 7.4 Levels

| Point | Level |
|-------|-------|
| AF_OUT nominal (60 % deviation) | 0.4 Vpp (**VERIFY**) |
| ADC input full scale | 3.3 Vpp; nominal signal set to -12 dBFS by buffer gain 2 |
| Wire PCM nominal | -12 dBFS (0.25 full scale) for 60 % deviation; app normalises mic to this |
| DAC output nominal | 1.0 Vpp |
| MIC_IN nominal | 20 mVrms (**VERIFY**), divider 1:30 |

Ground: audio op-amps and ADC/DAC references on +3V3A, star ground at the
MCU analog ground pin, kept away from the buck converter return path.

## 7.5 Data and digital voice through the same path

| Mode | TX source | RX sink | Module filters (`AT+SETFILTER`) |
|------|-----------|---------|---------------------------------|
| Voice | phone 16 kHz PCM -> x3 -> DAC | ADC -> /3 -> phone | emphasis, HPF, LPF active |
| AFSK 1200 Bd | `afsk_mod` at 48 kHz -> DAC | ADC -> /3 -> `afsk_demod` | as voice (standard for 1200 Bd) |
| FreeDV 2400B | `dv_tx_push` modem out 48 kHz -> DAC | ADC 48 kHz -> `dv_rx_push` | emphasis bypassed (flat), HPF/LPF active |
| Sound-card profile | UAC 48 kHz -> DAC directly | ADC -> UAC 48 kHz | app-selectable, all bypassable |
| 9600 Bd / M17 (future) | 48 kHz direct | 48 kHz direct | all bypassed; needs the modulator-bandwidth check T-A4 |

T-A4 (new): measure the module's TX frequency response from MIC_IN with all
filters bypassed, 20 Hz-6 kHz; a 9600 Bd mode needs roughly flat to 5 kHz
and usable response below 100 Hz. If the module fails, 9600 Bd modes stay
out of scope for this module (an alternative RF chip, not a redesign of the
audio path, would be needed).
