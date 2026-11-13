// SPDX-License-Identifier: GPL-3.0-or-later
//
// A zoo whose animals are poorly: as the next month begins, the park is fined for neglecting them, and told so.
//
//   ./nob script scripts/tests/awards.ts -- --no-audio

export {};

const kWeek = 90; // Calendar units
const kMonth = 360;
const kWeekSeconds = 150; // Of park time
const kNeglected = 30; // The fine for neglected animals, as park files number it

await park.open("Campaigns/Base/Wild/RunDownZoo.dat", { sandbox: false });
await park.ready();
await frames(30);
const poorly = animals.list().filter((a) => !a.crated && a.health < 0.4).length;
log("animals", animals.list().length, "poorly", poorly);
expect(poorly > 4, "more than four poorly animals as the zoo opens");
expect(!park.messages().some((m) => m.award === kNeglected), "no fine before the month is out");

// The park notes the month it is in as the first week ends, and looks at the awards as a week ends in a later
// month. Weeks end only as time runs, and the animals mend meanwhile: run to this week's end (its graphs take a
// sample), then set the calendar just short of a week's end a month on and run over it.
const sampled = () => JSON.stringify(park.history().cashLessLoan);
let samples = sampled();
await park.advanceUntil(() => sampled() !== samples, kWeekSeconds + 2, "the week's end");
park.setDate(park.date() + kMonth + kWeek - 1);
samples = sampled();
await park.advanceUntil(() => sampled() !== samples, 5, "a week's end a month on");
log("poorly as the month is judged", animals.list().filter((a) => !a.crated && a.health < 0.4).length);
const told = park.messages().filter((m) => m.award !== undefined);
log("awards", JSON.stringify(told));
expect(
    told.some((m) => m.award === kNeglected && m.key === "Award_Fine"),
    "the fine for neglected animals",
);
await screenshot("awards_fined");
quit(0);
