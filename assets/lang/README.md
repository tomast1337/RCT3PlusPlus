# Interface strings

Every string the interface shows, in our own words, one folder per language (named by language and region
code, `en-US`). The loader merges every `.json` file in the folder, so the split below is only for people;
a key may appear in only one file, and a key given twice stops the program with the file and key named.
The format is described in `src/ui/translation_file.hpp`.

| File | Holds |
|------|-------|
| `interface.json` | Windows, dialogs, menus, tooltips and messages: keys our code names, and keys the game's data names (research categories, music, animals) |
| `scenery.json` | Scenery item names (`SceneryItem_` and the item's name) |
| `tracks.json` | Track pieces (`Track_`) and track design names (`TrackDesign_`) |
| `guests.json` | Guests and staff: thoughts, states, carried items, animations, names |
| `tutorial.json` | Tutorial pages and their setup |
| `rct3pp.json` | Our own keys, which the game never had (`Rct3pp_`) |
| `unbuilt.json` | Game keys nothing reaches yet, kept for the features that will show them |

## Adding a key

- Text the game's windows already have a key for keeps that key. Text the game never had gets a key under
  `Rct3pp` in `rct3pp.json`, nested in groups as the file already is.
- Name the key in full in the code (`"Rct3pp_MyKey"`), not built from pieces, so the audit and the test find it.
- When a feature starts showing a key from `unbuilt.json`, move it to the file it now belongs in.
- A test (`tests/ui/translation_audit.test.cpp`) fails when one of our keys is used nowhere, and when the
  code names one of our keys, or looks a literal key up, that no file has.

## Finding where a string shows

Build, then run the audit from the repository root:

    ./nob build
    build/debug/lang-audit --report keys.tsv

It prints a count of keys by how they are reached and exits non-zero when the code names a key no file has.
The report gives each key's group, its file, and where it is reached: a `file:line` in the code, the pattern
it is built from, or the game data file that names it. With an install (`--game DIR` or `RCT3PP_GAME_PATH`)
it also tells keys the game's data names from keys only in the game's own key list.
