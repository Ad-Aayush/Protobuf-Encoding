# Protobuf Encoder (Scratch)

Minimal protobuf-like encoder/decoder primitives (varint, fixed64/double, length-delimited strings) with GoogleTest-based unit tests.

Install bear:
    sudo apt install bear

## Setup
Clone GoogleTest into third_party/:
    git clone https://github.com/google/googletest.git third_party/googletest

## Build and run tests
    make test

## Editor support
Generate the compilation database used by language servers:
    bear -- make -B

Reload the editor window after generating compile_commands.json.
