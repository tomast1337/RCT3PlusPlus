# Legal position

rct3++ is an independent, open-source program that loads the data files of RollerCoaster Tycoon 3 and runs
parks made with them. It is not affiliated with, endorsed by or connected to Atari, Frontier Developments or
any other rights holder. It ships no game files: you need your own copy of the game, and rct3++ reads it
from where you installed it.

This page says how the project is built, which laws touch it, and how much risk we think each one carries.
It is not legal advice (see the end).

## What the licence agreement says

The end-user licence agreement of RollerCoaster Tycoon 3 Complete Edition
([on Steam](https://store.steampowered.com/eula/1368820_eula_0?eulaLang=english)), section 1.2, forbids
reverse engineering, decompiling, disassembling or adapting the product, and making derivative works of it,
"in whole or in part". Every project that reads this game's files works in the shadow of that clause,
whatever method it uses.

## How the project is built

What is in this repository:

- Code and prose we wrote ourselves, under GPL-3.0-or-later.
- Tests whose inputs are built in code: invented values shaped like the formats, never bytes from a real
  file.
- Values borrowed from other open-source projects under a compatible licence, each credited where it is
  used (see [CLEAN_ROOM.md](../CLEAN_ROOM.md)).

What is not, and never will be:

- Game files, or anything extracted or converted from them: textures, models, sounds, text, saves.
- Decompiler or disassembler output, function names or addresses recovered from the game's program,
  hex dumps of real files, screenshots of the original game.

Where the knowledge comes from:

- **The data files.** Most of what rct3++ knows is the layout of the game's archives, saves and scenarios,
  learnt by reading the files on our own installs and checking each claim against all of them.
- **The running game.** Behaviour (how guests queue, how fast a ride cycles) is measured by playing it and
  approximated, or replaced by a design of our own. Matching the original exactly is not a goal.
- **Other projects' public documentation**, where it exists and agrees with the files.

## What could happen

**In short:** projects like this one, free reimplementations that need the player's own copy of the game,
have run in the open for many years (OpenRCT2, OpenTTD, OpenMW and others). When a rights holder does
object, the usual first step is a request to take the project down or to stop, not a lawsuit.

Three areas of law touch the project. They are listed from the one that would give an objection the most
weight to the one that would give it the least. This is a ranking between the three, not a forecast that
any of it will happen.

### 1. The licence agreement (contract)

**What could happen:** a rights holder says that studying the game broke the licence agreement, and asks
us to stop.

**Why it ranks first:** the agreement forbids reverse engineering outright, and breaking a contract does not
need anything to be copied. In the United States, courts have enforced such terms
(_Bowers v. Baystate Technologies_, 2003) and upheld shrink-wrap licences as contracts
(_ProCD v. Zeidenberg_, 1996).

**What softens it:** in the European Union, Directive 2009/24/EC lets a lawful user observe, study and test
a program while running it (article 5(3)), allows decompiling it where that is needed to make an
independent program work with it (article 6), and makes contract terms forbidding either void (article 8).
How far this reaches for a game and its data files has not been settled by a court that we know of.

### 2. Copyright

**What could happen:** a claim that rct3++ copies the game's code or content.

**What we do about it:** the repository holds only code and text we wrote, and no game content. Copyright
protects expression, not ideas, methods or file layouts, and courts have found studying a program for
compatibility to be fair use (_Sega v. Accolade_, 1992; _Sony v. Connectix_, 2000). That leaves little to
claim, though an argument about structure or derivative work can always be made.

### 3. Copy protection (DMCA)

**What could happen:** a claim that rct3++ gets around the game's copy protection.

**What we do about it:** it does not. It reads files already on the player's disk, from a game they
installed, and bypasses nothing. US law (17 U.S.C. § 1201(f)) also allows getting around protection where
that is needed only to make an independent program work with another.

### What keeps the risk low

- No game files, extracted assets or leaked material, ever (see [CLEAN_ROOM.md](../CLEAN_ROOM.md)).
- The player needs their own copy of the game; rct3++ replaces none of it.
- The project is free and sells nothing.
- No official logos or branding, and a clear statement that the project is not affiliated.

### If a rights holder gets in touch

We take it seriously and answer politely. We pause what they ask about while we get advice, rather than
arguing in public.

## Sources

Law:

- [Licence agreement, RollerCoaster Tycoon 3 Complete Edition](https://store.steampowered.com/eula/1368820_eula_0?eulaLang=english), Steam
- US Copyright Act, [17 U.S.C.](https://www.govinfo.gov/content/pkg/USCODE-2022-title17/html/USCODE-2022-title17.htm), including § 1201
- [Directive 2009/24/EC](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32009L0024) on the legal protection of computer programs

Cases:

- _Sega Enterprises Ltd. v. Accolade, Inc._, 977 F.2d 1510 (9th Cir. 1992)
- _ProCD, Inc. v. Zeidenberg_, 86 F.3d 1447 (7th Cir. 1996)
- _Sony Computer Entertainment, Inc. v. Connectix Corp._, 203 F.3d 596 (9th Cir. 2000)
- _Bowers v. Baystate Technologies, Inc._, 320 F.3d 1317 (Fed. Cir. 2003)

Background:

- Pamela Samuelson and Suzanne Scotchmer, [The Law and Economics of Reverse Engineering](https://doi.org/10.2307/797533), Yale Law Journal (2002)
- Electronic Frontier Foundation, [Coders' Rights Project: Reverse Engineering FAQ](https://www.eff.org/issues/coders/reverse-engineering-faq)

## Not legal advice

This page is our own reading, written by people who are not lawyers. It is not legal advice. Laws differ
between countries; if you need to rely on any of this, ask a qualified lawyer where you live.
