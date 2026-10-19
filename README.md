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
./nob test --sanitize --no-tools   # the same without building the tools
./nob test -- -ltc             # extra args go to the test binary (list test cases)
./nob fuzz ovl_header --seconds 60   # libFuzzer, needs Clang
./nob run                      # graphical viewer: front screen + Dear ImGui (needs GLFW, a Vulkan 1.3 driver, glslc)
./nob run -- --frames 120      # exit after 120 frames (smoke test); RCT3PP_VALIDATION=1 enables validation layers
./nob --release | ./nob clean
./nob test -j 4                # at most 4 compilers at once (NOB_JOBS=4 does the same); default one per core
./nob fetch                    # optional downloads into downloads/: the fallback font (GNU Unifont)
build/debug/ovl-dump "/path/to/Main.common.ovl"
build/debug/sound-play "/path/to/Main.common.ovl" SOUND_NAME   # play one sound item (omit the name to list them)
build/debug/sound-play --null --tone 440                       # same path without an audio device
```

`./nob` rebuilds itself when `nob.cpp` or `nob.hpp` change. `CXX` selects the compiler (default `g++`). Fuzzing uses `CLANGXX` (default `clang++`).
`build/compile_commands.json` is written for clangd. Needs a C++26 compiler (GCC 15+, Clang 20+). POSIX
only for now.

Audio (`src/audio/`) uses the vendored [miniaudio](third_party/miniaudio/) and is linked into the tests and
the tools that use it only. It needs no audio package and no device to build or test: on Linux miniaudio
loads ALSA, PulseAudio or JACK at run time, and falls back to its null backend when there is none.
The game's music is Windows Media Audio, and the first release's startup movies Windows Media Video 9, both
decoded by [a cut of FFmpeg](third_party/ffmpeg/) (LGPL-2.1-or-later) holding only those decoders, vendored
and built into the binary.

Files under `assets/` are built into every binary with `#embed`, so rct3++ ships as one file. Among them
are our own translations, `assets/lang/<Language>.json`, for text the game's string tables lack: a JSON
object of strings, nested objects joining their names with `_` (`{"Rct3pp": {"UsualKeys": "Usual keys"}}`
gives `Rct3pp_UsualKeys`). A language without a file, or a key a file lacks, uses English. Strings may name
values in braces, `{count} guests`, filled in when shown.

Text the game's fonts cannot draw (Greek, Cyrillic, arrows and other signs) falls back to
[GNU Unifont](https://unifoundry.com/unifont/), rendered with the vendored [stb_truetype](third_party/stb/).
The font is not in the repository: `./nob fetch` downloads a pinned release into `downloads/` and checks
its SHA-256. Unifont is dual-licensed under the SIL Open Font License 1.1 and GPL-2.0-or-later with the GNU
font embedding exception. Without it the viewer runs with the game's fonts alone.

## Configuration

rct3++ reads the original game's data from your own install. Tell it where that is in the settings file
or with an environment variable; `rct3pp --help` lists both, with the file's location.

Everything rct3++ keeps for you lives in one folder: `rct3pp/` under your data folder (`$XDG_DATA_HOME`,
else `~/.local/share`; `%APPDATA%` on Windows), or `$RCT3PP_DATA` when set. In it, `config/` holds the
settings (`settings.json`), what the options window sets (`options.json`) and the keys you bound
(`keys.json`); `screenshots/` and `music/` are for screenshots and your own park music. Pictures in
`screenshots/` (JPEG, PNG, BMP or TGA) join the game's own in the slideshow behind the front screen. The files are JSON,
read with the vendored [JSON for Modern C++](third_party/nlohmann/).

The load window lists the parks in `parks/` (and `scenarios/`, `new_scenarios/`) and then those the original
game saved, found in its `RCT3` folder in Documents (for a Steam install played through Proton, inside the
game's Proton prefix), or in `$RCT3PP_GAME_SAVES`. The game's own files are only read, never changed or
deleted. Saving writes rct3++'s own format, as `<name>.park` in `parks/`: the original park it
started from, kept whole, plus what you changed (ground, paths, objects, rides, money, date, camera,
guests, staff and patrol areas). Loaded guests walk on from where they stood; queues, rides in motion and
litter start again from the original park. Sandbox, New starts from the game's blank landscape.

`rct3pp --write-settings` writes the settings file with every setting, including any given on the same
command line; `null` leaves a setting at its default:

```
rct3pp --no-intro --scale 2 --write-settings
```

```json
{
    "game_path": "/path/to/RollerCoaster Tycoon 3",
    "skip_intro": true,
    "interface_scale": 2,
    "language": null
}
```

The command line wins over environment variables, which win over the file.

| Variable | Meaning |
|----------|---------|
| `RCT3PP_GAME_PATH` | Root folder of the installed game, the one that contains `gui/` and `Style/` |
| `RCT3PP_VALIDATION` | `1` enables the graphics API's validation layer |
| `RCT3PP_FALLBACK_FONT` | TrueType font for characters the game fonts lack (default `downloads/unifont-15.0.06.ttf`) |
| `RCT3PP_DATA` | Folder for your settings, keys, screenshots and music, instead of the default one |
| `RCT3PP_GAME_SAVES` | The original game's folder of your games (`RCT3` in its Documents), when it is not found by itself |

```
export RCT3PP_GAME_PATH="/path/to/RollerCoaster Tycoon 3"
```

File names inside the install are matched without regard to case, as on Windows.

## Scripts

`rct3pp --script FILE.js` runs a JavaScript module that plays the game through its own pointer: it opens
parks from the game's folder, points the camera, clicks widgets by name, drags over the ground, waits frames
or conditions, reads the park back and takes screenshots. The game exits when the script ends, with 1 when it
failed, so scripts serve as tests of the game as played. `scripts/types/rct3pp.d.ts` describes what scripts
get. Scripts run in the vendored [QuickJS-NG](third_party/quickjs/) (MIT), without its file, network or
system modules.

`./nob script` runs every script in `scripts/tests/`; `./nob script FILE` runs one, and arguments after `--`
go to the game. TypeScript is compiled with `tsc` first (`npm install -g typescript`); `.js` files run as
they are. Each script's log and named screenshots land in `build/scripts/report/NAME/` for review.

## Clean-room policy

rct3++ is a clean-room reimplementation. This repository contains original code and format descriptions
written in our own words, and nothing else: no game files or assets, no text or data copied from them, and
no decompiled or disassembled code. rct3++ ships no game content; you need your own legitimate copy of
RollerCoaster Tycoon 3 to play.

Where our knowledge comes from:

- **Data files, read as data.** rct3++ reads the game's data, save and scenario files so that you can use
  the files you own, as modding tools have long done. We describe their layout ourselves.
- **Watching the game play.** A value that exists only inside the game's program (a speed, a rate, a
  threshold) is treated as unknown. We measure the behaviour in the running game and approximate it, and
  the code says so.
- **Our own model**, where measuring is not practical. rct3++ does not aim to match the original number
  for number.
- **Open-source work** under a compatible licence, such as OpenRCT2, credited where it is used.

We do not accept contributions based on decompiling, disassembling or debugging the game's executables, or
on leaked source or documents. See the clean-room standards in the [Code of Conduct](CODE_OF_CONDUCT.md)
and the detailed rules in [CLEAN_ROOM.md](CLEAN_ROOM.md).

## Contributing

Everyone taking part follows the [Code of Conduct](CODE_OF_CONDUCT.md). Read it, and
[CLEAN_ROOM.md](CLEAN_ROOM.md), before opening a pull request.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).

RollerCoaster Tycoon is a registered trademark of Chris Sawyer. Frontier and the Frontier logo are
trademarks of Frontier Developments plc. This project is not affiliated with, endorsed by, or
sponsored by either.
