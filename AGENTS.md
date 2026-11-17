# AGENTS.md

Rules for AI agents (and humans) working in rct3++.

## Git: never commit

Agents must not commit, amend, rebase, push, tag or otherwise change git history. Leave changes in the
working tree. When a change is ready, suggest a commit message instead and let the maintainer commit.

Suggested messages:

- Imperative mood, subject at most 72 characters, no trailing period: `Parse v5 stream header block`.
- Body explains why, wrapped at 72 columns. Skip it when the subject says everything.
- Group unrelated edits into separate suggested commits rather than one large message.
- The maintainer adds their own `Signed-off-by` line. Do not add `Co-Authored-By` or other attribution
  lines.

## Project

- C++26, built with `nob` (`nob.cpp`, no external build system), GPL-3.0-or-later. Put `// SPDX-License-Identifier: GPL-3.0-or-later` at the top of
  every source file.
- Write to a C++23 baseline and use C++26 features only where GCC 16+ and Clang 20+ (on libstdc++ 16+) both support them. Nullable references are `std::optional<T&>`, not `T*`; raw pointers stay only at third-party edges (EnTT, C libraries).
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

## Original work

Read [ORIGINAL_WORK.md](ORIGINAL_WORK.md) before touching anything to do with the game's files. Summary:

- No game files or anything extracted from them, no byte dumps from real files, no code, names or
  addresses taken from the game's program, no copied code from other projects (vendored libraries in
  `third_party/` keep their own license files).
- Tests build their inputs in code (see `tests/ovl_header_test.cpp`). Never commit files from the game
  install.
- `local/` is git-ignored and holds fuzz corpora and other generated output. Never reference it from
  tracked files.
- Before suggesting a commit, review the diff as described in ORIGINAL_WORK.md.

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
Glance test: from the path alone you can tell what is inside, without opening it. A feature's data and the
functions that act on it share one `.hpp`/`.cpp` pair. Three or more files with one prefix become a folder,
and the prefix goes: `park/track_pieces.hpp` becomes `park/track/pieces.hpp`. The namespace follows the
folder: `src/park/track/` is `rct3::park::track`, and `app/a/b/` is `rct3::a::b` likewise.
Platform code goes in files whose name ends in `_windows` (built only on Windows), `_linux` (only on Linux) or
`_ffmpeg` (everywhere but Windows), beside a header every platform shares: `audio/wma/decoder.hpp` with
`decoder_ffmpeg.cpp`, `config/game/install.hpp` with `install_linux.cpp` and `install_windows.cpp`.

**Product code and fixtures stay apart.** Probes and fixtures are not mixed into the folders of the code
they check. Tests live in `tests/`, tools in `tools/`, library code in `src/`.

**A value that means something has one name, in one place.** Loader type ids, environment variable names
(`RCT3PP_*`), tables and tolerances are named once and used by that name, never retyped as a literal in
another file. Plain `0`, `1`, `0.5f`, and format offsets and sizes with a comment naming the format and
field, are fine. A new duplicate of an existing constant needs a reason in its commit message or PR. A tuning
number gets a `constexpr` beside its use (or a field in its area's tuning struct), typed with its unit, and a
one-line comment saying what it does; shared constants live once: gravity, the hour and percent in
`units/units.hpp`, hash constants in `core/hash.hpp`.

**Heap allocations: none again and again on hot paths.** What costs time is allocating on every frame or
tick, not the heap itself. Data loaded once (game data, meshes, textures, save trees), EnTT's pools, the
widget tree (it needs stable addresses), pimpl objects and the render device stay on the heap. Do not move
things to the stack for its own sake: the main thread has 8 MB, worker threads less, and emscripten 64 KB.
On a per-frame or per-tick path, in this order:

1. Reuse buffers. Keep the list in its owner (a member, or a buffers struct passed down like
   `simulation::StepBuffers`), `clear()` it each time, which keeps its memory, and fill it again. Do not
   return a fresh `std::vector` each frame. A per-node flag or count is a vector indexed by node, not a hash
   set or map.
2. Keep only what is used. If the code reads the nearest two, track two; do not collect and sort them all.
3. `std::array` for a count with a fixed maximum (wheels per car, corners of a tile), and
   `std::inplace_vector<T, N>` for a short list with a hard maximum.
4. `std::string_view` for a string that is only compared or looked up. Strings up to 15 characters do not
   allocate anyway.
5. A template parameter or `std::function_ref` for a callback that does not outlive the call. A
   `std::function` allocates once its captures are big.
6. A per-frame arena (`std::pmr::monotonic_buffer_resource` and `std::pmr` containers) only on a hot path
   shown by measurement; it adds noise everywhere it is used.

Measure before and after, with the same park and length: count allocations by call site (heaptrack, or a
sampling `malloc` preloaded into a debug build) and fix the top few. Compare a short run with a long one, so
loading drops out and only the steady cost per tick or frame is left. A rewrite for allocations keeps
behaviour identical: `sim-run` output must not change.

## Code style

- Parsing untrusted bytes (all game files) is bounds-checked and returns `std::expected`, never reads out of
  range, never trusts a count without checking it against the bytes left.
- No exceptions for expected failures such as truncated or unsupported files.
- Match the surrounding code's naming and comment density. Comments say why, not what.
- Warnings are on (`-Wall -Wextra -Wpedantic`). Do not leave new ones.
- Build text with `std::format`, not by joining strings with `+`:
  `std::format("cannot read {}: {}", path.string(), ec.message())`, not `"cannot read " + path.string() + ...`.
  Pass values straight in, without `std::to_string` or `std::string(view)`. Appending one piece with `+=` is
  fine. `std::u16string` text has no `std::format`, so it keeps `+`.
- Build a value with designated initializers (`T { .a = x, .b = y }`), fields in declaration order, not by
  declaring it and assigning field after field. Assign afterwards only fields that depend on control flow.
  Give every struct member a default (`{ }` when nothing else fits) so leaving a field out does not warn.
- A value with a unit has a unit type from `units/units.hpp` (`units::Money`, `units::Seconds`,
  `units::Radians`, ...), so the declaration says what it measures: `units::Seconds idle { 5 };`, not
  `float idle = 5; ///< Seconds`. Add a kind there when none fits, with one doc comment. Plain numbers stay at
  the edges a type cannot cross (file layouts, the GPU, third-party code) and in short locals.
- A place in one list that is passed around or kept has an id type from `core/id.hpp`
  (`using RideIndex = core::Id<struct RideIndexTag>;`, declared beside the list), so a ride's place cannot be
  passed where a stall's is meant: `void drop_ride_cars(Scene&, RideIndex ride)`, not `std::size_t ride`.
  Index the list with `.value`. Loop counters, file layouts, the GPU, EnTT and the script API's JSON numbers
  stay plain and are wrapped where they come in.
- No magic "none" values: a value that may be missing is `std::optional<T>` (or an enum when there are more
  than two states), not `-1` meaning "never". Where a file keeps `-1` for none, only the code that reads and
  writes it sees the number: a parser turns it into an optional (`core::none_if_negative`), and a park file
  member kept that way is named in `parkfile/kept_as_minus_one.hpp`.
- No bare `bool` arguments: a flag that changes what a function does is an `enum class` declared beside it, so
  the call says what it asks for (`hold_person(scene, at, size, Hold::GiveBack)`, not `..., false, true)`).
  Several flags go in a struct built with designated initializers (`{ .down = held, .shift = shift }`). A bool
  that is plainly the value being set (`set_open(bool open)`) or passed straight from a variable of that name
  stays.
- Named results: a function that returns two or more things, or a member that keeps them, uses a small struct
  with named fields (`struct NearestRoute { std::size_t target { }; std::vector<NodeId> walk { }; };`), not
  `std::pair` or `std::tuple`, so callers read `route->walk`, not `route->second`. A pair stays inside one
  function, and where a library wants one (`std::map` entries, keys built from several values).
- No comments that only say what: when a comment restates the name or the next line, rename the code so it says
  it (`std::size_t next_file = 0;`, not `std::size_t next = 0; ///< The next file to read`), or split a condition
  into a named local, then delete the comment. A comment stays when it says why: a reason, a source, a surprise,
  a rule of the game, a range or edge case, or a unit no type carries yet.

## Number types

The width of every number is written in its type, the same on every compiler and platform.

- Whole numbers use the fixed-width types from `<cstdint>`: `std::int32_t`, `std::uint8_t`, `std::int64_t` and
  so on, at the width the value needs. Not bare `int`, `unsigned`, `short` or `long`: their width depends on
  the compiler (`long` is 64-bit on Linux and macOS, 32-bit on Windows).
- Counts, sizes and indexes into containers are `std::size_t`.
- `float` is 32-bit and `double` 64-bit IEEE 754; `units/units.hpp` checks this at compile time. Do not use
  `std::float32_t` / `std::float64_t` until Clang and MSVC have them.
- A measured quantity (distance, speed, angle, time, money) uses its unit type from `units/units.hpp`.
- A field read from or written to a file uses the width the format has (`std::uint16_t` for a 2-byte field).
- Exceptions: `int main(int argc, char** argv)`; `char` for text; `unsigned char` to pass a `char` to the
  `<cctype>` functions (`std::tolower`, `std::isdigit`, ...); and a type a C or library API fixes (a callback
  signature, an `int*` out-parameter, a printf-style argument). Mark such a line with a `// c-api` comment.
