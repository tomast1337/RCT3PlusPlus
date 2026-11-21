<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Windows

How to configure, build, and run rct3++ on 64-bit Windows with
[MSYS2](https://www.msys2.org/) CLANG64. Every command here runs in the **MSYS2
CLANG64** shell. Other shells, such as Git Bash, do not put `/clang64/bin` on
`PATH`, so `clang++` is not found there.

CLANG64 is the one MSYS2 environment rct3++ builds in: rct3++ is built with
Clang on every platform, and CLANG64 is MSYS2's Clang environment, with LLVM's
standard library (libc++) and linker. CI and the release packages use it too.
libc++ has no `std::stacktrace` yet, so a crash report on Windows comes without
a stack.

## Requirements

- The installed game, whose root folder contains `gui/` and `Style/`.
- A Vulkan 1.3 driver.
- The packages below, installed once in the CLANG64 shell:

```
pacman -S --needed git mingw-w64-clang-x86_64-clang mingw-w64-clang-x86_64-libc++ mingw-w64-clang-x86_64-vulkan-loader
```

| Package                                | Provides                                                            |
| -------------------------------------- | ------------------------------------------------------------------- |
| `git`                                  | The version shown in the game (`git describe`)                      |
| `mingw-w64-clang-x86_64-clang`         | `clang++` (23 or later), `ld.lld`, `windres` and `clang-format`     |
| `mingw-w64-clang-x86_64-libc++`        | The C++ standard library                                            |
| `mingw-w64-clang-x86_64-vulkan-loader` | `libvulkan-1.dll.a`, for the viewer; `./nob test` builds without it |

GLFW, Dear ImGui and the Vulkan headers are vendored, so they need no package.

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
`clang-format` from `PATH`, which the `clang` package above installs. Formatting
differs between releases, so use the version CI pins
(`.github/workflows/ci.yml`) when the two disagree. `./nob tidy` needs
`clang-tidy`:

```
pacman -S --needed mingw-w64-clang-x86_64-clang-tools-extra
```

The scripts' half of `./nob format` (Prettier, through `npx`) is skipped on
Windows: `npx` is a script, which `nob` cannot start there. CI checks it.

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
clang++ -std=c++26 -o nob nob.cpp
```

Should create `nob.exe` with no errors. `./nob test` builds the library and the
tools and runs the tests; it needs neither the game nor a graphics driver. Then
build and start the viewer:

```
./nob run
```

Should open the game window. `./nob run -- --frames 120` exits after 120 frames.
Other `nob` commands are listed in
[Building and running](../CONTRIBUTING.md#building-and-running).

WMA music and WMV movies are decoded by Windows' own Media Foundation codecs
(`src/audio/wma/decoder_windows.cpp`, `src/video/wmv/decoder_windows.cpp`), so
the build downloads and compiles no FFmpeg. Windows N editions come without
them until the Media Feature Pack is installed.

The programs `nob` builds take UTF-8 as their code page (Windows 10 version 1903
or later), so the game's folder, your user folder and file names may hold any
characters. `nob` itself does not: keep the checkout in a folder whose path is
plain ASCII.

A debug build needs the CLANG64 shell's DLLs (`libc++.dll` and others in
`/clang64/bin`) to run. `./nob package` makes the release packages in
`build/dist/`, as the release workflow does; their `rct3pp.exe` runs without
MSYS2 (it also needs `zip`: `pacman -S --needed zip`).

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
| `bash: clang++: command not found`                                      | Wrong shell, or direnv replaced `PATH` | Use the CLANG64 shell; see [direnv](#direnv)                      |
| `cannot find -lvulkan-1`                                                | `vulkan-loader` package missing        | Install the packages under [Requirements](#requirements)          |
| `slangc: command not found`                                             | `slangc` is not on `PATH`              | Copy it into `/usr/local/bin` as above                            |
| `cannot start clang-format: The system cannot find the file specified.` | `clang-format` is not installed        | Install `clang-format`, see [Formatting Tools](#formatting-tools) |
