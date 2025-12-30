# Protobuf Encoder (Scratch)

Minimal protobuf-like encoder/decoder primitives (varint, fixed64/double, length-delimited strings) with GoogleTest-based unit tests.

## Setup
Clone GoogleTest into `third_party/`:
```bash
git clone https://github.com/google/googletest.git third_party/googletest
```

## Build and run tests
```bash
make test
```

## Editor support
Generate the compilation database used by language servers:

### Install bear
```bash
sudo apt install bear
```

### Use bear to generate `compile_commands.json`
```bash
bear -- make -B
```
