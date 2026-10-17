# Clean room: keeping game material out of the repository

This repository is public-facing. Nothing that comes from the game, or from reading the game, belongs in it.
This guide is mainly for AI agents, who see game files and decompiler output constantly and copy things
into the tree without noticing.

## The rule

If it exists because of the game, and not because we wrote it, it stays out of git.

Original code, original prose and synthetic test data are in. Everything below is out.

## Never put in the repository

**Assets and game data**
- Any file from the game install: `.ovl`, `.dat`, `.ftx`, `.shs`, `.chk`, textures, models, sounds, fonts,
  GUI skins, text tables, scenario and save files. Includes "just a small one" and truncated copies.
- Extracted or converted forms of them: PNG/WAV/OBJ exports, atlases, string tables, symbol name lists,
  glyph maps, palettes.
- Hex dumps, `od`/`xxd` output or byte arrays copied from a real file, even short ones. Header bytes of a
  real file are still a copy of the file.
- Screenshots or recordings of the original game.

**Derived documentation**
- Decompiler output: Ghidra listings, pseudocode, call graphs, struct dumps, RTTI dumps, function names
  recovered from the binary, and addresses of functions in `RCT3.exe`.
- Ghidra project files (`.gpr`, `.rep`, `.lock`), exported databases, scripts that embed the above.
- Copied text from game manuals, in-game text, or the original developer's documentation or SDK.
- Long verbatim quotes from other projects' docs or code. Link to them instead.

**Local paths and environment**
- Absolute paths into the game install or the user's home directory.
- Steam IDs, product keys, account names, emails of people who are not contributors.

## What is fine

- Our own prose describing a file layout: field offsets, sizes, meanings, version differences, written in
  our words, with sample values only where they are needed to be unambiguous.
- Original code that parses those layouts.
- Synthetic fixtures built in code (see `tests/ovl_header_test.cpp`): invented strings and numbers shaped
  like the format, not taken from a real file.
- Loader and format names that are part of the file format itself (for example the type tags a parser must
  recognise). Keep these to what the parser needs.

When unsure whether something is a description or a copy: if you would have to look at the game file to
produce it character for character, it is a copy.

## Other open-source projects

OpenRCT2 (GPL-3.0, <https://github.com/OpenRCT2/OpenRCT2>) re-implements a game whose simulation is close to
ours, and its licence is compatible with ours. We may take inspiration from how it models the park, and
borrow simulation values (rates, thresholds, weights) where they fit. We do not copy its code or its
structure. Anything borrowed, even one constant, carries a comment that names the source exactly:

```cpp
// Taken from OpenRCT2 (GPL-3.0), src/openrct2/<path>, <function or constant name>.
```

The same goes for any other project: credit at the place of use, and only under a compatible licence.

## Values only the program knows

Some numbers are not in any data or save file, only inside the game's program: speeds, rates, thresholds,
timings. Treat them as unknown. Do not take them from the executable, even as a check.

- Measure the behaviour in the running game instead: an experiment anyone can repeat (build this, time
  that, count those), and approximate the value from the result.
- Or design the behaviour ourselves, when measuring is not practical. Matching the original exactly is not a goal.
- Mark it in the code, saying where it came from:

```cpp
constexpr float kQueueStep = 0.8f; ///< Approximation: timed guests shuffling along a 20-tile queue in-game
constexpr float kHuddleRadius = 0.7f; ///< Chosen by eye: a party of four stands apart
```

Values read from data or save files are fine to use; say which file they come from.

## Where game-derived material goes

Long-term private notes live in a workspace outside this repository, not here. `local/` at the repository
root is git-ignored and is for short-lived scratch: dumps, exports, decompiler excerpts and anything else
game-derived that you need while working. It exists on one machine only.

- Agents may create and write files in `local/` freely.
- Agents must not reference `local/` contents or the private workspace from tracked files: no `#include`,
  no doc links, no paths, no pasted excerpts. A tracked file must build and read correctly on a machine
  where both are absent.
- `.gitignore` also blocks common game extensions as a safety net. Do not rely on it, and do not add
  negations (`!pattern`) for them.

## Before suggesting a commit

Agents do not commit (see `AGENTS.md`), but they must check what they are handing over:

1. `git status` and `git diff`: read every added file and hunk, not only the code.
2. Search the diff for game-derived content:
   - `git diff | grep -nE '0x[0-9a-fA-F]{6,}'` for binary addresses and byte runs.
   - Look for absolute paths (`/Games`, `/home`, `steamapps`) and file extensions listed above.
   - Look for long literal arrays or strings in tests.
3. If any of it is game-derived, move it to `local/` and rewrite the tracked text in your own words.
4. Say what you checked when you suggest the commit message.

## If something leaked

Stop and tell the maintainer before doing anything else. Do not try to fix it by committing a deletion:
the content stays in history and the history needs rewriting. Do not push.
