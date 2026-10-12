# AGENTS.md

Rules for AI agents (and humans) working in rct3++.

## Git: never commit

Agents must not commit, amend, rebase, push, tag or otherwise change git history. Leave changes in the
working tree. When a change is ready, suggest a commit message instead and let the maintainer commit.

Suggested messages:

- Imperative mood, subject at most 72 characters, no trailing period: `Parse v5 sub-version header block`.
- Body explains why, wrapped at 72 columns. Skip it when the subject says everything.
- Group unrelated edits into separate suggested commits rather than one large message.
- The maintainer adds their own `Signed-off-by` line. Do not add `Co-Authored-By` or other attribution
  lines.

## Project

- C++26, built with `nob` (`nob.cpp`, no external build system), GPL-3.0-or-later. Put `// SPDX-License-Identifier: GPL-3.0-or-later` at the top of
  every source file.
- Write to a C++23 baseline and use C++26 features only where GCC 15+ and Clang 20+ both support them.
- Bootstrap once with `g++ -std=c++26 -o nob nob.cpp`, then `./nob test` builds and runs the tests.
  New `.cpp` files under `src/`, `tools/<name>/main.cpp` and `tests/` are picked up automatically.
- Tests use [doctest](third_party/doctest/). Put them in `tests/<mirror of the src path>/<topic>.test.cpp`,
  one `TEST_CASE` per behaviour, `SUBCASE` for variants. Run `./nob test --sanitize` before handing over
  parser changes. Test inputs are built in code, never read from the game install.
- Fuzz targets live in `fuzz/<name>.cpp` (libFuzzer, Clang). Every new parser entry point that takes
  untrusted bytes gets one. Run `./nob fuzz <name> --seconds 60`. Corpora and crashes go to `local/`.
  Seeds under `fuzz/seeds/` and `fuzz/*.dict` must be synthetic.
- Format specifications, when published, live in this repository's `docs/`, written in our own words. Fix
  the spec in the same change that corrects the parser.
- Verify format claims against real game files, not only against existing docs. `ovl-dump` over the game
  install is the oracle.

## Clean room

Read [CLEAN_ROOM.md](CLEAN_ROOM.md) before touching anything derived from the game. Summary:

- No game assets, no decompiler output, no hex dumps from real files, no recovered binary addresses or
  function names, no copied code from other projects in this repository (vendored libraries in `third_party/` keep their
  own license files).
- Tests build their inputs in code (see `tests/ovl_header_test.cpp`). Never commit files from the game
  install.
- Game-derived notes and scratch output go in `local/`, which is git-ignored. Never reference it from
  tracked files.
- Before suggesting a commit, review the diff for game-derived content as described in CLEAN_ROOM.md.

## Design

**Data first, objects where they fit.** Default to plain data (a struct when it is small and short-lived,
which also keeps per-frame code free of heap allocations) and free functions that take that data and
everything else they need as parameters. A class is fine when something has identity, a lifetime or large
mutable data: a loaded resource, a cache, a GPU buffer, a session. Build bigger things by composing small
ones. Use inheritance only for real polymorphism, and an interface only when two implementations exist.
Do not bend a design into structs where a class is the natural fit.

**Explicit state by default.** Prefer state that is a value created at startup and passed down, so a
function's inputs are visible at its call site. Hidden state (mutable statics, singletons, `thread_local`,
ambient context) is a cost, not a ban. Legitimate cases: process-wide services (logging, profilers, the
graphics device), caches, callbacks that native or platform code forces to be static, and per-thread
buffers kept for speed. Each one gets a short comment saying why it is global and who owns it. A new one
needs that reason in its commit message or PR.

**Pass dependencies explicitly.** A function gets what it needs as parameters, and passing is the goal,
not something to avoid. Group related things into a context value (a simulation context, a render
context) and pass that down, including to players or actors. Out: containers and service locators that
build and wire objects at runtime, and one globally reachable instance of a context. Construction stays
visible in code, so you can see where each dependency comes from.

**Structured flow.** Short functions that read top to bottom, explicit branches and loops, no webs of
callbacks or events. A pipeline is a function that calls its stages in order.

**One topic per file, and the path says what it is.** Folder = domain, file name = what the code does.
Glance test: from the path alone you can tell what is inside, without opening it.

**Product code and fixtures stay apart.** Probes and fixtures are not mixed into the folders of the code
they check. Tests live in `tests/`, tools in `tools/`, library code in `src/`.

**A value that means something has one name, in one place.** Loader type ids, environment variable names
(`RCT3PP_*`), tables and tolerances are named once and used by that name, never retyped as a literal in
another file. Plain `0`, `1`, `0.5f`, and format offsets and sizes with a comment naming the format and
field, are fine. A new duplicate of an existing constant needs a reason in its commit message or PR.

## Code style

- Parsing untrusted bytes (all game files) is bounds-checked and returns `std::expected`, never reads out of
  range, never trusts a count without checking it against the bytes left.
- No exceptions for expected failures such as truncated or unsupported files.
- Match the surrounding code's naming and comment density. Comments say why, not what.
- Warnings are on (`-Wall -Wextra -Wpedantic`). Do not leave new ones.
