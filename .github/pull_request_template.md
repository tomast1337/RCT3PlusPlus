## What and why

<!-- What this change does and why. Link the issue it closes: "Closes #123". -->

## How it was checked

<!-- Tests added or run, and what you looked at in the running app, if anything. -->

## Checklist

- [ ] `./nob test` passes, and `./nob format --check` is clean
- [ ] New behaviour has tests, with inputs built in code
- [ ] No new compiler warnings
- [ ] Format descriptions in `docs/` are updated if a parser changed
- [ ] Clean room: no game files, extracted assets, byte dumps, decompiler output, recovered names or
      addresses, and no absolute paths, in the code, tests, docs or this description
      ([CLEAN_ROOM.md](../CLEAN_ROOM.md))
- [ ] Values borrowed from another project name their source at the place of use
