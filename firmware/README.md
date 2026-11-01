# firmware

Portable modules + application skeleton for the mobile-radio pack. See
`docs/08-firmware.md` for the architecture.

```sh
make test        # unit tests on the host (gcc)
make check-app   # application syntax check against hal.h + host port stub
make all
```

Modules (`include/`, `src/`): `mrp` protocol, `pmr446` channel plan,
`sa818` radio module AT driver, `pd_policy` USB-PD role policy, `ptt_guard`
transmit interlocks. `hal.h` is the port API; `port/host/` is a stub;
`port/stm32f405/` (CubeMX + TinyUSB) is the next step.
