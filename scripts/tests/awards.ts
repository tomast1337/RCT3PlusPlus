// SPDX-License-Identifier: GPL-3.0-or-later
//
// A zoo whose animals are poorly: as the next month begins, the park is fined for neglecting them, and told so.
//
//   ./nob script scripts/tests/awards.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

const kMonth = 360; // Calendar units
const kNeglected = 30; // The fine for neglected animals, as park files number it

await park.open("Campaigns/Base/Wild/RunDownZoo.dat", { sandbox: false });
await park.ready();
await frames(30);
const poorly = animals.list().filter((a) => !a.crated && a.health < 0.4).length;
log("animals", animals.list().length, "poorly", poorly);
expect(poorly > 4, "more than four poorly animals as the zoo opens");
expect(!park.messages().some((m) => m.award === kNeglected), "no fine before the month is out");

// On to the start of the next month, when the awards are looked at.
park.setDate((Math.floor(park.date() / kMonth) + 1) * kMonth + 1);
await park.advanceUntil(() => park.messages().some((m) => m.award !== undefined), 5, "the month's awards");
const told = park.messages().filter((m) => m.award !== undefined);
log("awards", JSON.stringify(told));
expect(told.some((m) => m.award === kNeglected && m.key === "Award_Fine"), "the fine for neglected animals");
await screenshot("awards_fined");
quit(0);
