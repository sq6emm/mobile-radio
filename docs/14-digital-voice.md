# 14. Digital voice: FreeDV 2400B

## 14.1 Why this mode

| Candidate | Vocoder | Modem / path | Verdict |
|-----------|---------|--------------|---------|
| **FreeDV 2400B** | Codec2 1300 bit/s | 2-FSK-class modem at 48 kHz designed to pass through the mic/speaker path of an ordinary FM radio | **Chosen.** Open (LGPL 2.1), proven on the STM32F405 (the SM1000 is exactly that chip), no hardware change. Interoperates with other FreeDV 2400B stations. |
| M17 | Codec2 3200 | 4FSK 4800 sym/s needs a flat modulator path to DC and ~5 kHz | Only if T-A4 shows the SA818's bypassed path is flat enough; otherwise it needs a different RF chip (CC1200 class). Kept as a software option. |
| DMR / dPMR446 | AMBE+2 (licensed) | 4FSK, standard-defined | Excluded: vocoder licensing and type approval for digital PMR446. |
| Codec2 over LoRa | Codec2 700C/1300 | LoRa SF7 BW250 (~11 kbit/s) | Possible pack-to-pack novelty, not interoperable; not planned. |

## 14.2 How it is wired

```
phone mic 16 kHz --MRP AUDIO_TX--> dv_tx_push: 16->8 kHz, Codec2 encode, modem -> 48 kHz PCM -> DAC -> SA818 MIC_IN (flat path)
SA818 AF_OUT -> ADC 48 kHz -> dv_rx_push: modem demod (freedv_nin() sized blocks), Codec2 decode, 8->16 kHz --MRP AUDIO_RX--> phone
```

- `SET_DV 1` (HAM mode only) opens the codec, bypasses the module's
  pre/de-emphasis (`AT+SETFILTER=1,0,0`) and the hardware 300 Hz high-pass
  (`hal_audio_flat_path`), and routes audio through `dv.c` instead of the
  resamplers. `SET_DV 0` or leaving HAM mode restores analogue voice.
- Frame timing: 40 ms speech frames (320 samples at 8 kHz) produce 1920 modem
  samples at 48 kHz; the receiver consumes `freedv_nin()` samples per call
  (the modem adjusts it for clock offset). `dv.c` buffers accordingly and is
  unit tested with a transparent stand-in for the library.
- PTT, time-out, arbitration and interlocks are identical to analogue voice;
  the arbiter kind is `TX_FM_DV` (same priority as voice).
- The STATUS frame reports DV enabled and sync; the pack LED blinks the sync
  state so standalone use (button PTT) also works in DV.

## 14.3 Resource and licence notes

- CPU: Codec2 1300 + the 2400B modem take about a third of an STM32F405;
  measured on the SM1000. Budget in docs/08.
- Flash/RAM: Codec2 with only the 1300 mode and the 2400B modem compiled in
  (`CODEC2_MODE_EN_DEFAULT=0`, enable 1300 and 2400B) is well under 200 KB
  flash and 40 KB RAM.
- Licence: LGPL 2.1 (`firmware/third_party/codec2/COPYING`). Publish the
  firmware source or at least the object files and link instructions.
- Audio levels: FreeDV expects the FM path to be roughly flat 300-2800 Hz
  with the 2400B tones; set the DAC drive for ~2.5 kHz deviation (T-A5).

## 14.4 What stays open

- Real round-trip test of `dv.c` against the compiled library (docs/08: not
  run in the environment this was written in).
- Listening tests on the real FM path: deviation setting, squelch tail
  behaviour (open squelch while DV is enabled, the modem's own sync decides).
- M17 feasibility measurement (T-A4).
