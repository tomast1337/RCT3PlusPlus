<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Windows

How to configure, build, and run rct3++ on 64-bit Windows with
[MSYS2](https://www.msys2.org/) UCRT64. Every command here runs in the **MSYS2
UCRT64** shell. Other shells, such as Git Bash, do not put `/ucrt64/bin` on
`PATH`, so `g++` is not found there.

## Requirements

- The installed game, whose root folder contains `gui/` and `Style/`.
- A Vulkan 1.3 driver.
- The packages below, installed once in the UCRT64 shell:

```
pacman -S --needed make mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-glfw mingw-w64-ucrt-x86_64-vulkan-loader
```

| Package                               | Provides                                     |
| ------------------------------------- | -------------------------------------------- |
| `make`                                | GNU `make`, which FFmpeg's `configure` needs |
| `mingw-w64-ucrt-x86_64-gcc`           | `g++`; rct3++ needs GCC 16 or later          |
| `mingw-w64-ucrt-x86_64-glfw`          | `libglfw3.dll.a`                             |
| `mingw-w64-ucrt-x86_64-vulkan-loader` | `libvulkan-1.dll.a`                          |

The shaders are compiled with `slangc`, which MSYS2 does not package. Download
`slang-<version>-windows-x86_64.zip` from the
[Slang releases](https://github.com/shader-slang/slang/releases), then copy
`slangc.exe` and the DLLs beside it into a folder on `PATH`, such as
`C:\msys64\usr\local\bin` (`/usr/local/bin` in the shell). Check it:

```
slangc -v
```

Should print the Slang version.

## Formatting Tools

A change needs `./nob format --check` clean
([Sending a change](../CONTRIBUTING.md#sending-a-change)). It runs
`clang-format` from `PATH`. Install it in the UCRT64 shell:

```
pacman -S --needed mingw-w64-ucrt-x86_64-clang-tools-extra
```

Check the install:

```
clang-format --version
```

## Configure

rct3++ reads the game's data from your install. Set `RCT3PP_GAME_PATH` to its
root folder, in MSYS2 path form:

```
export RCT3PP_GAME_PATH="/c/path/to/RollerCoaster Tycoon 3"
```

Put the line in `~/.bashrc` (`C:\msys64\home\<user>\.bashrc` for a default
install) to keep it. The other variables and the settings file are described in
[Configuration](../CONTRIBUTING.md#configuration).

## Build and Run

Run both steps from the repository root. The first bootstraps the build script:

```
g++ -std=c++26 -o nob nob.cpp
```

Should create `nob.exe` with no errors. Then build and start the viewer:

```
./nob run
```

Should open the game window. `./nob run -- --frames 120` exits after 120 frames.
Other `nob` commands are listed in
[Building and running](../CONTRIBUTING.md#building-and-running).

The first build downloads the pinned FFmpeg source into `downloads/` and runs
its `configure` once, into `build/ffmpeg/`. The cut in
[third_party/ffmpeg](../third_party/ffmpeg/) is compiled against the `config.h`
that writes, because the one in the repository was made on Linux. Delete
`build/ffmpeg/` to run `configure` again.

## direnv

Using [direnv](https://direnv.net) is optional.

### Caveats

A Windows build of `direnv.exe` (such as the Chocolatey package) exports `PATH`
in Windows form when an `.envrc` changes it, but MSYS bash cannot read it (see
[direnv/direnv#253](https://github.com/direnv/direnv/issues/253)).

The MSYS2 shell reads `~/.config/direnv`, where `~` is `C:\msys64\home\<user>`.

## Troubleshooting

| Message                                                                 | Cause                                  | Fix                                                               |
| ----------------------------------------------------------------------- | -------------------------------------- | ----------------------------------------------------------------- |
| `bash: g++: command not found`                                          | Wrong shell, or direnv replaced `PATH` | Use the UCRT64 shell; see [direnv](#direnv)                       |
| `cannot find -lglfw3`                                                   | `glfw` package missing                 | Install the packages under [Requirements](#requirements)          |
| `cannot find -lvulkan-1`                                                | `vulkan-loader` package missing        | Install the packages under [Requirements](#requirements)          |
| `slangc: command not found`                                             | `slangc` is not on `PATH`              | Copy it into `/usr/local/bin` as above                            |
| `cannot start clang-format: The system cannot find the file specified.` | `clang-format` is not installed        | Install `clang-format`, see [Formatting Tools](#formatting-tools) |
