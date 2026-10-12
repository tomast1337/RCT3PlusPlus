# rct3++

Open-source reimplementation of RollerCoaster Tycoon 3 Complete Edition, in C++26.

Status: early. The OVL container header parser and `ovl-dump` exist. Formats are being
reverse engineered bottom-up: asset formats, then saves and scenarios, then the simulation, then
the renderer (Vulkan + Dear ImGui).

## Build

The build script is `nob.cpp`, plain C++ that builds itself. Bootstrap once, then use `./nob`:

```
g++ -std=c++26 -o nob nob.cpp
./nob                          # build library, tools and tests into build/<mode>/
./nob test                     # build and run tests
./nob test --filter header     # only test cases whose name contains "header"
./nob test --sanitize          # AddressSanitizer + UBSan
./nob test -- -ltc             # extra args go to the test binary (list test cases)
./nob fuzz ovl_header --seconds 60   # libFuzzer, needs Clang
./nob --release | ./nob clean
build/debug/ovl-dump "/path/to/Main.common.ovl"
```

`./nob` rebuilds itself when `nob.cpp` or `nob.hpp` change. `CXX` selects the compiler (default `g++`). Fuzzing uses `CLANGXX` (default `clang++`).
`build/compile_commands.json` is written for clangd. Needs a C++26 compiler (GCC 15+, Clang 20+). POSIX
only for now.

## Clean-room policy

This repository contains original code and format specifications only. It must not contain
decompiled source, game assets, or other copyrighted material from RollerCoaster Tycoon 3. You need a
legitimate copy of the game to use it.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).

RollerCoaster Tycoon is a registered trademark of Chris Sawyer. Frontier and the Frontier logo are
trademarks of Frontier Developments plc. This project is not affiliated with, endorsed by, or
sponsored by either.
