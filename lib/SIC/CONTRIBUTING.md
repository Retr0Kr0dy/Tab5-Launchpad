# Contributing to SIC

## Coding style
- C99 for C sources, C++17 for Arduino shims.
- No RTTI/exceptions. Keep heap spikes small.
- Error codes: `0` = OK, negative for errors (errno-like).

## PR checklist
- [ ] Builds `examples/Cardputer-Demo` @ `cardputer-io-stable`
- [ ] No duplicate symbols / link conflicts
- [ ] Docs updated if API changes
- [ ] Add/extend a native test if possible

## Running CI locally
```bash
pio run -d SIC/examples/Cardputer-Demo -e cardputer-io-stable
```
