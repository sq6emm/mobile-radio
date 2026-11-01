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
| `include/ptt_guard.h`, `src/ptt_guard.c` | transmit interlocks: keep-alive, TOT, cool-down, inhibit, busy lockout; voice pre-empts data |
| `bandplan` | PMR446 / HAM 70 cm modes, callsign unlock, frequency and power rules |
| `ax25`, `afsk` | AX.25 UI frames, HDLC, FCS; Bell 202 modulator and demodulator (16/48 kHz) |
| `aprs`, `msg`, `beacon` | APRS positions/messages/acks; message retries and dedupe; SmartBeaconing |
| `arbiter` | who may transmit (voice = DV > data); voice pre-empts data |
| `resample` | 48 <-> 16 kHz FIR |
| `dv` | FreeDV 2400B glue around the Codec2 library (submodule `third_party/codec2`) |
| `pd_policy` | PHONE port role policy, now with the pass-through rule (`pd_policy_set_external_power`) |
| `include/hal.h` | the port API the application needs |
| `src/app_main.c` | application super-loop and MRP handlers |
| `port/host/hal_host.c`, `main_host.c`, `freedv_stub.c` | stub port and FreeDV stand-in so the whole application links on Linux (`build/app_host`) |
| `tests/` | unit tests (`make test`), `vectors.c` reference generator for the Python cross-check |
| `third_party/codec2` | git submodule, LGPL 2.1; not built on the host by default |

Target port (`port/stm32f405/`, not yet written) = CubeMX-generated clock,
USB OTG FS, TIM6-triggered ADC1/DAC1 DMA at 16 kHz, USART1 to the module,
I2C1 to PD/charger/gauge, plus TinyUSB with a vendor class instance (and
CDC / UAC instances optional). Nothing in `src/` touches a register.

## 8.2 Main loop

```
init: parser, pd_policy, ptt_guard, bandplan, arbiter, msg, beacon, resamplers,
      AFSK demod -> HDLC -> on_ax25_frame, radio_init, USB profile
loop @ 1 kHz:
  kick watchdog
  USB bulk in -> mrp_parser_feed -> on_frame()
  local PTT button -> ptt_guard_request(LOCAL); sound-card profile: CDC RTS -> ptt_guard_request(HOST)
  voice keyed? -> arbiter_request(FM_VOICE | FM_DV) (pre-empts data)
  keyed = ptt_guard_tick() -> hal_radio_ptt, audio paths
  data_tx_service(): generate AFSK chunk-wise into the DAC ring; unkey when drained
  audio_rx_service(): ADC 48 kHz -> [DV rx] or [/3 -> AUDIO_RX to phone + AFSK demod]
  every 1 s: power status of both ports, pd_policy_set_external_power + pd_policy_tick,
             charge throttling, inhibit, beacon_due -> send, msg_next_tx -> send, STATUS
```

Audio is DMA driven so the loop's jitter does not matter; the loop copies or
generates at most a few ms of audio per pass. AFSK is synthesised 64 bits at
a time into the DAC ring instead of pre-rendering a whole packet (a 2 s
packet at 48 kHz would need 192 KB).

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

## 8.3b Operating modes and identity

`bandplan` holds the mode. PMR446 accepts only `SET_CHANNEL`; HAM accepts
`SET_FREQ` anywhere on the 12.5 kHz grid between 430 and 440 MHz. HAM is
locked until `SET_IDENTITY` stores a syntactically valid callsign
(`bandplan_callsign_valid`: 3-7 upper-case alphanumerics, a digit, a letter,
ending in a letter). The callsign-SSID becomes the AX.25/APRS
source address; in PMR mode the nickname is used instead. Switching back to
PMR turns digital voice off.

## 8.3c Data paths

- **FM data TX** (`data_tx_start`): APRS info -> AX.25 UI (tocall `APZMRP`,
  path from the beacon settings) -> HDLC with 45 preamble flags (300 ms) ->
  keyed as `PTT_SRC_DATA` through `ptt_guard` and `arbiter` -> AFSK at 48 kHz
  into the DAC ring -> unkey when the ring has drained. Voice pre-emption
  aborts the frame; `msg` retries later.
- **FM data RX**: the 16 kHz voice stream also feeds the AFSK demodulator
  whenever the squelch is open (or squelch is 0); decoded frames go to the
  host raw (`DATA_RX`) and through `msg_on_rx` / `aprs_decode`
  (`MSG_RX`, `APRS_RX`, automatic acks on the same transport).
- **Digital voice**: `SET_DV` opens FreeDV 2400B (HAM only), bypasses the
  module's emphasis and the hardware 300 Hz high-pass for a flat path;
  `AUDIO_TX` frames go through `dv_tx_push` (48 kHz modem audio to the DAC),
  the ADC stream through `dv_rx_push` (speech back to the phone at 16 kHz).

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

- `pd_policy` decides swaps and source enable on the PHONE port; the port
  turns actions into TPS25751 commands (`SWSr`/`SWDF` 4CC tasks), TPS55288
  set-point/enable writes and BQ25798 input-limit writes.
- CHARGE port: the STUSB4500 negotiates on its own; the application reads
  the contract once a second, sets the BQ25798 VAC2 input limit, and calls
  `pd_policy_set_external_power()` so that sourcing to the phone continues
  below the reserve (pass-through). Thermal throttling reduces the battery
  charge current first (`hal_power_charge_throttle`), the phone's power last.
- VSYS min 6.2 V.
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

Target toolchain: `arm-none-eabi-gcc`, TinyUSB 0.16+, STM32CubeF4 HAL/LL,
Codec2 built for Cortex-M4F (the SM1000 build flags are the reference).

Note on the host build of Codec2: `make check-app` links `freedv_stub.c`, a
transparent stand-in, so `dv.c` is exercised without the vocoder. A real
round trip (`PCM -> FreeDV 2400B -> PCM`) on the host needs the submodule
compiled with CMake; that build was not run in the environment where this
code was written (executing freshly downloaded third-party build systems was
not permitted there), so it is the first thing to do on a developer machine:

```sh
cmake -S firmware/third_party/codec2 -B /tmp/codec2-build -DCMAKE_BUILD_TYPE=Release -DUNITTEST=OFF
cmake --build /tmp/codec2-build -j --target codec2
# then link build/libmr.a + src/dv.c against /tmp/codec2-build/src/libcodec2.a instead of the stub
```

## 8.7 CPU budget (to be measured in Phase C)

| Task | Estimate on STM32F405 @ 168 MHz |
|------|---------------------------------|
| FreeDV 2400B TX or RX (Codec2 1300 + FSK modem) | 30-40 % (SM1000 figures) |
| AFSK demodulator at 16 kHz (4 correlators x 13 taps) | ~3 % |
| Resamplers 48 <-> 16 kHz, 96 taps | ~4 % |
| USB vendor bulk + CDC | ~3 % |
| UAC2 48 kHz isochronous (sound-card profile; DV is off then) | ~5 % |
| MRP, PD/charger control | ~2 % |

DV and the sound-card profile are never active at the same time, so the
worst case is DV + AFSK RX + resampling + USB bulk at roughly 50 %. If the
measurement lands above 70 %, the STM32F427 (same pins, 180 MHz, more RAM)
or an STM32H7 is the escalation.
