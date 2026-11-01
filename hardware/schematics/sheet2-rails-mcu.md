# Sheet 2: rails and MCU

## U7 TPS62130 (+4V0, 3 A)

| Pin | Net |
|-----|-----|
| VIN, PVIN | VSYS (10 uF + 100 nF) |
| EN | RADIO_RAIL_EN (MCU, pulled low at reset) |
| FSW | +3V3D (2.5 MHz) ; PG unused ; DEF = +3V3D forced PWM (check datasheet pin function) |
| SW | L2 1.0 uH 3 A |
| VOS/FB | divider for 4.00 V: R1 = 365 k, R2 = 100 k with 0.8 V reference -> V = 0.8 x (1 + 365/100) = 3.72... use R1 = 400 k, R2 = 100 k -> 4.00 V |
| OUT | +4V0_RAW -> L3 1 uH + 22 uF -> FB 600R -> +4V0 (100 uF + 100 nF + 1 nF at the module) |

## LDOs

| Part | In | Out | Load |
|------|----|-----|------|
| U8 TPS7A2033 | +4V0 | +3V3D | MCU, PD controller, gauge, LEDs, module UART level |
| U9 LP5907-3.3 | +4V0 | +3V3A | op-amps, mic bias, VREF+ of MCU |

+3V3D is always on (needed to configure the PD controller); the MCU runs
from it and enables the radio rail with `RADIO_RAIL_EN`.

## U1 STM32F405RGT6 pin plan

| Function | Pins | Net |
|----------|------|-----|
| USB FS | PA11 DM, PA12 DP | USB_DM, USB_DP (22 ohm series) ; PA9 VBUS sense through divider (20 V tolerant divider 10:1) |
| USART1 to module | PA9?? no: use USART2 PA2 TX, PA3 RX | RADIO_TX, RADIO_RX, 1 k + 100 pF RC at module |
| Module control | PB0 PTT (open-drain, 10 k pull-up to +3V3D), PB1 PD, PB2 H/L, PB10 SQ (EXTI, pull-up) | |
| ADC1_IN0 PA0 | AUDIO_RX (from AA filter) | TIM6 trigger 16 kHz, DMA2 |
| ADC2_IN1 PA1 | MIC_LOCAL | same trigger |
| ADC1_IN4 PA4? reserved for DAC: DAC1_OUT1 = PA4 | AUDIO_TX | DMA1, TIM6 trigger |
| ADC3_IN10 PC0, IN11 PC1 | NTC_PA, NTC_BOARD | |
| I2C1 PB6 SCL, PB7 SDA | I2C1 | 2.2 k pull-ups to +3V3D |
| IRQs PB8 PD_IRQ, PB9 CHG_IRQ, PB12 GAUGE_ALERT | EXTI | |
| GPIO PC2 RADIO_RAIL_EN, PC3 SPK_EN, PC4 MIC_EN, PC5 CHG_CE, PC6 CHG_OTG, PC7 TEST_PORT_SENSE | | TEST_PORT_SENSE: MM8130 has no switch sense; use a pogo/ground contact under the cover instead |
| Buttons PC8 PTT_BTN, PC9 PWR_BTN (wake-capable: use PA0 WKUP for PWR_BTN and move AUDIO_RX to PA5/ADC1_IN5) | | |
| LEDs PC10, PC11 | | |
| SWD PA13/PA14, NRST, BOOT0 | debug header | |
| HSE 8 MHz Y1 | PLL -> 168 MHz core, 48 MHz USB | 168 MHz x n never lands in 446.0-446.2 MHz (2.655x); 48 MHz x 9 = 432 MHz: keep clock traces short |
| VREF+ | +3V3A via ferrite | |
| VDDA | +3V3A | |

Decoupling 100 nF per VDD pin + 4.7 uF, VCAP 2 x 2.2 uF.
