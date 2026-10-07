# Board Bring‑Up in ~15 Minutes

1. Pick a base under `src/boards/` and set pins via `build_flags`.
2. Call `sic_begin()` from `setup()` and watch serial at 115200.
3. Sanity: I2C traffic log, mic start/read/stop, optional beep.
4. Expected logs:
```
[DEMO] SIC HAL v2 (verbose)  SIC 1.0.0
[OK] sic_begin (rc=1)
[SYS] model=ESP32 ...
```
5. Pitfalls:
- Matrix keyboard: verify pull-ups & scan timings.
- Duplicate symbols: disable dummy/autoreg drivers via `-DSIC_DISABLE_DUMMY=1`.
