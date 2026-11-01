# 7. Audio path and audio filtering

Half-duplex voice, 300-3000 Hz, FM deviation +/- 2.5 kHz max at 12.5 kHz
spacing. Two audio sources and two sinks exist and are switched by the PTT
state and by whether a phone is attached.

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
   buffer, then a 2nd-order Sallen-Key low-pass at 5 kHz (Butterworth:
   R 10 k / 10 k, C 2.2 nF / 4.7 nF), biased at 1.65 V. With 16 kHz sampling
   the alias band starts at 13 kHz where this filter gives ~17 dB; the
   module's own 3 kHz roll-off adds > 30 dB there.
3. **ADC**: STM32 ADC1, 12-bit, timer-triggered at exactly 16.000 kHz,
   DMA double buffer of 320 samples (20 ms). Firmware subtracts the DC bias,
   applies a 1st-order 250 Hz high-pass to kill residual CTCSS/hum, and
   scales to s16.
4. **Speaker (standalone)**: TPA6211A1 class-AB 3 W mono into an 8 ohm,
   28 mm speaker; class-AB rather than class-D so no switching products reach
   the receiver. Enabled only when no host is attached or the app asks.

## 7.2 Transmit side

1. **From the phone**: 20 ms PCM frames into a DAC ring buffer (3 frames deep,
   starts playing when 2 are queued).
2. **DAC**: STM32 DAC1 12-bit at 16 kHz, DMA, output 0.2-3.1 V.
3. **Reconstruction filter**: 2nd-order Sallen-Key 4 kHz (removes the 16 kHz
   image), then a 1st-order 300 Hz high-pass (keeps the voice band clear for
   the CTCSS tone that the module adds below 250 Hz).
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
| Anti-alias LPF | hardware, RX | 5 kHz 2nd-order | ADC at 16 kHz |
| Reconstruction LPF | hardware, TX | 4 kHz 2nd-order | DAC image |
| Band HPF | hardware, TX | 300 Hz 1st-order | keep voice above CTCSS band |
| DC/hum HPF | firmware, RX | 250 Hz 1st-order IIR | DC bias, residual tone |
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
