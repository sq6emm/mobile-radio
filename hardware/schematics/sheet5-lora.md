# Sheet 5: LoRa modules and antenna switch

## U14 E22-400M22S (433 MHz) and U15 E22-900M22S (868 MHz)

Both modules have the same pinout family (**VERIFY** against the Ebyte
datasheet revision): GND, VCC (3.3 V), NSS, SCK, MOSI, MISO, NRST, BUSY,
DIO1, DIO2, RXEN, TXEN, ANT.

| Module pin | U14 (433) net | U15 (868) net | Notes |
|------------|---------------|---------------|-------|
| VCC | +3V3R | +3V3R | 10 uF + 100 nF at each module; TX peak 120 mA each |
| NSS | LORA433_NSS (PB12) | LORA868_NSS (PA15) | separate chip selects |
| SCK / MOSI / MISO | SPI2 PB13 / PB15 / PB14 | same bus | 8 MHz, 33 ohm series on SCK/MOSI |
| NRST | LORA433_RST (PC12) | LORA868_RST (PD2) | |
| BUSY | LORA433_BUSY (PB4) | LORA868_BUSY (PB5) | input |
| DIO1 | LORA433_IRQ (PB3, EXTI) | LORA868_IRQ (PC13, EXTI) | TxDone / RxDone / Timeout |
| TXEN / RXEN | LORA433_TXEN / RXEN (PC14 / PC15) | LORA868_TXEN / RXEN (PA8 / PA10) | driven by the driver's rf_switch callback |
| DIO2 | NC | NC | DIO2-as-switch not used |
| ANT | RF_L433 (to U16 SPDT RF2) | RF_L868 (to ANT2 match) | 50 ohm CPW |

## U16 PE4259 SPDT (helix sharing)

| Pin | Net |
|-----|-----|
| RFC | RF_SW_C -> BPF input (sheet 3) |
| RF1 | RF_LPF (from the FM low-pass, sheet 3) |
| RF2 | RF_L433 (from U14 ANT), with D4 BAP64-02 shunt PIN limiter footprint to GND through 100 pF |
| CTRL | ANT_SEL (PC11): low = FM, high = LoRa 433 (`hal_rf_antenna_to_lora433`) |
| VDD | +3V3R, 100 nF |

DC blocks 100 pF on RF1/RF2/RFC. Layout: the switch sits between the FM LPF
output and the BPF input inside the RF shield; keep RF1-RF2 isolation by
grounding the gap (the 25 dB figure degrades quickly with coupling).

## ANT2: 868 MHz printed inverted-F

```
RF_L868 --C 10 pF (series, tuning pad)--+-- IFA feed
                                       |
                                     L pad (shunt, NP default)
IFA short pin 4 mm from the feed to the ground plane edge
```

Keep-out 40 x 8 mm at the top-right PCB corner, no copper on any layer,
>= 30 mm from the helix base, >= 20 mm from the magnet ring. Ground plane
edge straight along the element.

## +3V3R

U17 TPS7A2033 from +4V0: 10 uF in/out, feeds U14, U15, U16 only.
