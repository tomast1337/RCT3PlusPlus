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

## The three bodies of law

### Contract: high risk

The licence agreement is a contract with the buyer. In _Bowers v. Baystate Technologies_ (Fed. Cir. 2003)
a US court enforced a licence term against reverse engineering even though the same act could have been
fair use under copyright. _ProCD v. Zeidenberg_ (7th Cir. 1996) upheld shrink-wrap licences as contracts in
the first place.

Breach of contract does not need any copying: the clause covers reverse engineering as such. A rights
holder who objects can ask for the project to stop.

In the European Union the picture differs: Directive 2009/24/EC lets a lawful user observe, study and test
a program while running it (article 5(3)), allows decompiling it when that is needed for interoperability
with an independently created program (article 6), and makes contract terms that forbid either void
(article 8). How far this covers a game, and its data files rather than its program, has not been settled
by a court that we know of.

### Copyright: medium risk

Copyright protects expression, not ideas, methods or file layouts.

- _Sega Enterprises v. Accolade_ (9th Cir. 1992): disassembling a program to reach the unprotected
  facts needed for compatibility was fair use.
- _Sony Computer Entertainment v. Connectix_ (9th Cir. 2000): copying and studying a console's firmware
  to build a compatible emulator was fair use, as an intermediate step that did not end up in the product.

Our code is original and our documents describe layouts in our own words, so we believe the published
work holds no protected expression of the game. That weakens an infringement claim a lot. It does not
remove it: an argument about structure or about derivative work can still be made.

### Anti-circumvention (DMCA): medium-low risk

17 U.S.C. § 1201 forbids getting around access controls. rct3++ does not bypass any copy protection or
DRM: it reads files the player already has on disk, from the game the player installed. Section 1201(f)
also exempts circumvention done only to make an independently created program interoperate, though that
exemption does not override a contract.

### Summary

| Area      | Risk       | Why                                                                     |
| --------- | ---------- | ----------------------------------------------------------------------- |
| Contract  | High       | The licence forbids reverse engineering, and US courts have enforced that |
| Copyright | Medium     | Original code and own-words specifications leave little to claim        |
| DMCA      | Medium-low | No protection is bypassed; files are read from the player's own install |

The likeliest outcome of an objection is a takedown request or a cease-and-desist letter, not a lawsuit.
If a rights holder contacts the project, we will take it seriously and answer.

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
