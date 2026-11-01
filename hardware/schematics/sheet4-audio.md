# Sheet 4: audio

## RX: AF_OUT -> ADC and speaker

```
AF_OUT --1u (AC couple)--+-- U11A buffer (gain 2, biased 1.65 V from +3V3A divider)
                         |        |
                         |     Sallen-Key LPF 5 kHz: R 10k/10k, C 2.2n (to bias) / 4.7n (feedback)
                         |        |
                         |      AUDIO_RX -> PA0 ADC1_IN0 (1 k + 1 nF)
                         +-- U12 TPA6211A1 IN- (via 10 k, gain set 2), SHDN = SPK_EN  -> LS1 8 ohm
```

## TX: DAC -> MIC_IN

```
DAC1_OUT1 (PA4) --> U13A Sallen-Key LPF 4 kHz (R 10k/10k, C 2.7n/5.6n)
                --> 1st-order HPF 300 Hz (C 100 nF, R 5.1 k to bias)
                --> divider 30:1 (R 15 k series, 510 ohm to GND) --> 100 ohm --> MIC_IN (1 nF at module)
```

Divider ratio is a placeholder until the module's microphone sensitivity is
measured (docs/07, T-R5c).

## Local microphone

```
MK1 SPU0410LR5H: VDD = +3V3A via 100 ohm + 10 uF, OUT --1u--> U13B non-inverting gain 30 dB
  (R 1 k / 33 k, bias 1.65 V) --> 1st-order HPF 300 Hz --> MIC_LOCAL -> PA1 ADC2_IN1
MIC_EN gates the mic supply through a P-FET.
```

Grounding: AGND island under U11/U13/MK1 joined to GND at the MCU VSSA pin.
Speaker amp returns to GND (not AGND).
