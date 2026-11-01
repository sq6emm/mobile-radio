# 8. Firmware

Code in `firmware/`. Portable modules have no hardware dependency and are
unit tested on the host with `make -C firmware test`; the application and the
port layer are checked with `make -C firmware check-app`.

## 8.1 Layout

| Path | What |
|------|------|
| `include/mrp.h`, `src/mrp.c` | MRP framing, CRC, parser with resync, payload pack/unpack |
| `include/pmr446.h`, `src/pmr446.c` | channel plan, CTCSS table, validation, frequency formatting |
| `include/sa818.h`, `src/sa818.c` | AT command builder / response parser for the radio module |
| `include/pd_policy.h`, `src/pd_policy.c` | USB-PD role policy state machine (docs/03) |
| `include/ptt_guard.h`, `src/ptt_guard.c` | transmit interlocks: keep-alive, TOT, cool-down, inhibit, busy lockout |
| `include/hal.h` | the port API the application needs |
| `src/app_main.c` | application super-loop and MRP handlers |
| `port/host/hal_host.c` | stub port, compiles on Linux |
| `tests/` | unit tests, `make test` |

Target port (`port/stm32f405/`, not yet written) = CubeMX-generated clock,
USB OTG FS, TIM6-triggered ADC1/DAC1 DMA at 16 kHz, USART1 to the module,
I2C1 to PD/charger/gauge, plus TinyUSB with a vendor class instance (and
CDC / UAC instances optional). Nothing in `src/` touches a register.

## 8.2 Main loop

```
init: parser, pd_policy, ptt_guard, radio_init (AT+DMOCONNECT, filters, volume, channel)
loop @ 1 kHz:
  kick watchdog
  USB bulk in  -> mrp_parser_feed -> on_frame()
  local PTT button -> ptt_guard_request(LOCAL)
  keyed = ptt_guard_tick()  -> hal_radio_ptt, audio paths follow the key state
  if !keyed and host: drain ADC ring in 320-sample frames -> MRP AUDIO_RX
  every 1 s: power status, pd_policy_tick, thermal inhibit, STATUS frame, LEDs
```

Audio is DMA driven so the loop's jitter does not matter; a 20 ms frame is
only ever copied, never produced, in the loop.

## 8.3 Interlocks (all in `ptt_guard`, tested)

| Interlock | Behaviour |
|-----------|-----------|
| Host keep-alive | PTT from the host is released if no frame arrives within `ttl_ms` (default 2 s). Audio frames count as keep-alive. |
| Transmit time-out | 180 s continuous, then 10 s cool-down in which PTT is refused. |
| Inhibit | PA/cell temperature > 70 C, SoC < 5 %, module not responding: PTT refused and an active transmission is dropped. |
| Busy-channel lockout | optional per channel: PTT refused while the squelch is open. |
| Priority | first keyer wins; the other source can neither steal nor release it. |
| Power level cap | the H setting is only allowed when the stored antenna gain keeps ERP <= 0.45 W. |
| Sequencing | radio rail enabled after the MCU drives PTT high (inactive); PTT line has a pull-up so a reset MCU cannot key. |

## 8.4 Radio module driver

`sa818.c` only builds strings. The sequence after power-up: wait 500 ms,
`AT+DMOCONNECT` (expect `+DMOCONNECT:0`), `AT+SETFILTER=0,0,0`,
`AT+DMOSETVOLUME=4`, `AT+DMOSETGROUP=0,446.0063,446.0063,0000,4,0000`. Each
command is retried 3 times with a 300 ms line timeout. Frequencies are
formatted with 4 decimals rounded to 100 Hz; the module's synthesiser grid is
finer than the rounding error (the datasheet says "XXX.XXXX"; **VERIFY** that
the module accepts `446.0063` and lands on 446.00625 within +/-100 Hz; if a
5-decimal form is accepted, change `pmr446_format_mhz`). RSSI is polled once a
second while receiving; raw-to-dBm uses a linear fit (`sa818_rssi_to_dbm`)
calibrated at the RF test port.

## 8.5 Power management

- `pd_policy` decides swaps and source enable; the port turns actions into
  TPS25751 commands (`SWSr`/`SWDF` 4CC tasks) and BQ25798 register writes.
- Charger: input current limit follows the PD contract; OTG voltage follows
  the source contract; VSYS min 6.2 V.
- Sleep: with no host and no activity for 5 min the MCU enters STOP mode,
  wakes on the PTT button, the SQ line, USB attach or the gauge alarm; the
  radio stays in receive (65 mA) unless the user turns it off with a long
  press (then only the gauge and PD controller run: < 1 mA).

## 8.6 Build and test

```sh
make -C firmware test        # unit tests (gcc)
make -C firmware check-app   # application compiles against hal.h
python3 tools/test_mrp.py    # Python protocol implementation agrees with C
```

Target toolchain: `arm-none-eabi-gcc`, TinyUSB 0.16+, STM32CubeF4 HAL/LL.
