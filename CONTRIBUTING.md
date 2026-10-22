# Contributing to rct3++

Thank you for wanting to help. Bug reports, format findings, code, tests and documentation are all
welcome.

Before anything else, read the [Code of Conduct](CODE_OF_CONDUCT.md), including its clean-room standards,
and [CLEAN_ROOM.md](CLEAN_ROOM.md). They decide what can be accepted, and a contribution that breaks them
is turned down however good it is.

## Reporting a bug or asking for a feature

Open an issue and pick the form that fits. Describe what you see in rct3++ and, when it matters, what the
original game does, in words. Do not attach game files, dumps or screenshots of the original game.
Your own saves may be attached so we can reproduce a problem; they are used for testing only and never
added to the repository.

Security problems (for example a file that crashes the parser or reads out of bounds) go through
[SECURITY.md](SECURITY.md), not a public issue.

## Sending a change

1. Open an issue first for anything larger than a small fix, so we can agree on the approach.
2. Build and test (see the [README](README.md)):

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
