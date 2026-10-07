# TODO

## Nice-to-have

- IR TX: replace dummy with real RMT-based `ir_tx` driver (ESP-IDF RMT or Arduino wrapper).
- SD (SPI): minimal driver wired to Arduino `SD` lib or `sdmmc_host`.
- Charger TP4057: add ADC-based VBAT and charge current sampling.
- Unit tests for registry with multiple drivers per same function ID.
- CI: PlatformIO build for all example envs.
