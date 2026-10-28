# rct3++

<p align="center"><img src="assets/icon/panda.svg" alt="rct3++ panda" width="200"></p>

An open-source reimplementation of RollerCoaster Tycoon 3 Complete Edition, in C++26.

rct3++ is a reimplementation, not a decompilation. It reads the game's data, save and scenario files from
your own copy, and rebuilds the game around them in new code. It will not be a 1:1 match of the original:
where the original's behaviour cannot be read from its files, such as the guest cap or ride physics,
rct3++ uses its own approximations, measured from the game as played or designed from scratch.

rct3++ ships no game content. You need your own legitimate copy of RollerCoaster Tycoon 3 to play.

Status: in development, not yet complete.

## Getting started

How to build rct3++, point it at your game install, configure it and run its test scripts is in
[CONTRIBUTING.md](CONTRIBUTING.md#building-and-running).

## Clean-room policy

rct3++ is a clean-room reimplementation. This repository contains original code and format descriptions
written in our own words, and nothing else: no game files or assets, no text or data copied from them, and
no decompiled or disassembled code.

Where our knowledge comes from:

- **Data files, read as data.** rct3++ reads the game's data, save and scenario files so that you can use
  the files you own, as modding tools have long done. We describe their layout ourselves.
- **Watching the game play.** A value that exists only inside the game's program (a speed, a rate, a
  threshold) is treated as unknown. We measure the behaviour in the running game and approximate it, and
  the code says so.
- **Our own model**, where measuring is not practical. rct3++ does not aim to match the original number
  for number.

We do not accept contributions based on decompiling, disassembling or debugging the game's executables, or
on leaked source or documents. See the clean-room standards in the [Code of Conduct](CODE_OF_CONDUCT.md)
and the detailed rules in [CLEAN_ROOM.md](CLEAN_ROOM.md).

## Contributing

Everyone taking part follows the [Code of Conduct](CODE_OF_CONDUCT.md). Read it, and
[CLEAN_ROOM.md](CLEAN_ROOM.md), before opening a pull request. [CONTRIBUTING.md](CONTRIBUTING.md) says how
to build and run rct3++, report a bug or send a change, and [SECURITY.md](SECURITY.md) how to report a security problem.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).

RollerCoaster Tycoon is a registered trademark of Chris Sawyer. Frontier and the Frontier logo are
trademarks of Frontier Developments plc. This project is not affiliated with, endorsed by, or
sponsored by either. The project's [legal position](docs/legal.md) explains the risks we
see and how we work around them.
