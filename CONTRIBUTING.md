# Contributing to rct3++

Thank you for wanting to help. Bug reports, format findings, code, tests and documentation are all
welcome.

Before anything else, read the [Code of Conduct](CODE_OF_CONDUCT.md), including its clean-room standards,
and [CLEAN_ROOM.md](CLEAN_ROOM.md). They decide what can be accepted, and a contribution that breaks them
is turned down however good it is.

## Building and running

### Build

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
./nob run                      # graphical viewer: front screen + Dear ImGui (needs GLFW, a Vulkan 1.3 driver, slangc)
./nob run -- --frames 120      # exit after 120 frames (smoke test); RCT3PP_VALIDATION=1 enables validation layers
./nob --release | ./nob clean
./nob test -j 4                # at most 4 compilers at once (NOB_JOBS=4 does the same); default one per core
./nob fetch                    # optional downloads into downloads/: the fallback font (GNU Unifont)
build/debug/ovl-dump "/path/to/Main.common.ovl"
build/debug/sound-play "/path/to/Main.common.ovl" SOUND_NAME   # play one sound item (omit the name to list them)
build/debug/sound-play --null --tone 440                       # same path without an audio device
```

`./nob` rebuilds itself when `nob.cpp` or `nob.hpp` change. `CXX` selects the compiler (default `g++`). Fuzzing uses `CLANGXX` (default `clang++`).
`build/compile_commands.json` is written for clangd. Needs a C++26 compiler (GCC 16+, or Clang 20+ with libstdc++ 16+). POSIX
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

### Configuration

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

### Scripts

`rct3pp --script FILE.js` runs a JavaScript module that plays the game through its own pointer: it opens
parks from the game's folder, points the camera, clicks widgets by name, drags over the ground, waits frames
or conditions, reads the park back and takes screenshots. The game exits when the script ends, with 1 when it
failed, so scripts serve as tests of the game as played. `scripts/types/rct3pp.d.ts` describes what scripts
get. Scripts run in the vendored [QuickJS-NG](third_party/quickjs/) (MIT), without its file, network or
system modules.

`./nob script` runs every script in `scripts/tests/`; `./nob script FILE` runs one, and arguments after `--`
go to the game. TypeScript is compiled with `tsc` first (`npm install -g typescript`); `.js` files run as
they are. Each script's log and named screenshots land in `build/scripts/report/NAME/` for review. Each
script runs with a fresh data folder of its own, `build/scripts/data/NAME/`, so what one script saves never
reaches another or your own settings; when `RCT3PP_DATA` is set, each folder starts as a copy of it. A folder
whose `new_scenarios/` is empty gets the install's Vanilla Hills park there, to start custom scenarios from.

## Reporting a bug or asking for a feature

Open an issue and pick the form that fits. Describe what you see in rct3++ and, when it matters, what the
original game does, in words. Do not attach game files, dumps or screenshots of the original game.
Your own saves may be attached so we can reproduce a problem; they are used for testing only and never
added to the repository.

Security problems (for example a file that crashes the parser or reads out of bounds) go through
[SECURITY.md](SECURITY.md), not a public issue.

## Sending a change

1. Open an issue first for anything larger than a small fix, so we can agree on the approach.
2. Build and test (see [Building and running](#building-and-running)):

   ```
   g++ -std=c++26 -o nob nob.cpp
   ./nob test
   ./nob format --check
   ```

   Parser changes also run `./nob test --sanitize`, and a new parser entry point that reads untrusted
   bytes gets a fuzz target in `fuzz/`.
3. Follow the design and code style in [AGENTS.md](AGENTS.md): plain data and free functions where they fit,
   bounds-checked parsing that returns `std::expected`, `std::format` for text, no new warnings.
4. Add tests for new behaviour under `tests/`, mirroring the `src/` path. Test inputs are built in code,
   never read from a game install.
5. Commit messages follow `type(scope): subject` (`feat`, `fix`, `docs`, `refactor`, `test`, `chore`),
   with a short body saying why. Sign off your commits (`git commit -s`): by doing so you certify the
   [Developer Certificate of Origin](https://developercertificate.org/), including that the work is
   yours to contribute.
6. Open the pull request and fill in its checklist.

## Where knowledge may come from

In short: the data files read as data, the running game observed and measured, our own design, and
openly licensed work credited where it is used. Values that live only inside the game's program are
treated as unknown and approximated from what the game does; the code says so. The Code of Conduct has
the full list, and the [legal position](docs/legal.md) explains why.

If you are unsure whether something is allowed, ask in an issue before writing it.

## Format documentation

Format descriptions live in [docs/](docs/), written in your own words, with invented examples. A change
that corrects a parser corrects its description in the same pull request. Check every claim against the
files of a real install (`ovl-dump` is the reference), but never paste their bytes.

## Licence

rct3++ is GPL-3.0-or-later. By contributing you agree that your work is released under that licence.
