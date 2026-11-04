# Original work: keeping game material out of the repository

rct3++ is original code and original documentation. It contains no files from the game, nothing extracted
or converted from them, no code or text copied from the game or its developer, and no decompiled or
disassembled code. File formats are described in our own words, and the game's behaviour is written in
our own code. Contributions follow the same rules.

## The rule

If something exists because of the game, and not because we made it, it is not part of this project.

The project is made of original code, original writing and invented test data. Its numbers come from the
game's data and save files, from measuring the original game in play, or from our own design, and each
one says which.

## Not accepted

**Game files and anything made from them**
- Files from the game install: `.ovl`, `.dat`, `.ftx`, `.shs`, `.chk`, textures, models, sounds, fonts,
  interface skins, text tables, scenarios and saves, including small or truncated copies.
- Extracted or converted forms of them: image, sound or model exports, atlases, string tables, name lists,
  glyph maps, palettes.
- Byte dumps or byte arrays copied from a real file, however short.
- Screenshots or recordings of the original game.

**Copied or derived text and code**
- Code, names or addresses taken from the game's program.
- Text from the game, its manuals, or its developer's documentation or tools.
- Long quotes from other projects' code or documentation. Link to them instead.

**Personal details**
- Absolute paths into a game install or a home directory.
- Account names, product keys, and contact details of people who are not contributors.

## Accepted

- Descriptions of file layouts in our own words: field offsets, sizes, meanings and version differences,
  with example values only where they are needed to be unambiguous.
- Original code that reads and writes those layouts.
- Test data built in code from invented values shaped like the format (see `tests/ovl_header_test.cpp`).
- Type tags and other names that are part of a file format and that a parser must recognise.

If you could only produce something character for character by looking at a game file, it is a copy.

## Marking where a number comes from

- **Data and save files:** say which file the value comes from.
- **Measured in play:** describe an experiment anyone can repeat, and approximate the value from it.
- **Our own design:** say what the value does. Matching the original exactly is not a goal.

```cpp
constexpr float kQueueStep = 0.8f; ///< Approximation: timed guests shuffling along a 20-tile queue in-game
constexpr float kHuddleRadius = 0.7f; ///< Chosen by eye: a party of four stands apart
```

## Other open-source projects

OpenRCT2 (GPL-3.0, <https://github.com/OpenRCT2/OpenRCT2>) re-implements a game whose simulation is close to
ours, and its licence is compatible with ours. We may take ideas from how it models the park and borrow
simulation values (rates, thresholds, weights) where they fit. We do not copy its code or its structure.
Anything borrowed, even one constant, names its source where it is used:

```cpp
// Taken from OpenRCT2 (GPL-3.0), src/openrct2/<path>, <function or constant name>.
```

The same goes for any other project: credit at the place of use, and only under a compatible licence.

## Local output

`local/` at the repository root is git-ignored and holds fuzz corpora, crash inputs and other generated
output. Tracked files never refer to it: a tracked file must build and read correctly without it.

## Before you commit

1. Read every added file and hunk in `git diff`, not only the code.
2. Check for long hex numbers and byte runs (`git diff | grep -nE '0x[0-9a-fA-F]{6,}'`), absolute paths,
   game file extensions, and long literal arrays or strings in tests.
3. Anything that fails these checks stays out of the commit.

`.gitignore` blocks common game file extensions as a safety net. Do not rely on it, and do not add
exceptions for them.

## If something gets in

Tell the maintainer before doing anything else. A deleting commit does not remove it: it stays in history,
which has to be rewritten. Do not push.
