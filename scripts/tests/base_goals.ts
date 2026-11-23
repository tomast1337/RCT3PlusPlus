// SPDX-License-Identifier: GPL-3.0-or-later
//
// Opens each scenario of the original career's base game in turn and prints its goals level by level, with how
// each reads against the park once the first day's check has run, and its VIP visits. A goal the simulation
// cannot judge, or one that has no figure to read, is named: such a level could never be won. The listing also
// goes to base_goals.txt in the report folder.
//
//   ./nob script scripts/tests/base_goals.ts -- --no-intro --no-audio --seed 1 [Scenario...]

export {};

const kScenarios = [
    "VanillaHills",
    "Goldrush!",
    "BoxOffice",
    "ValleyOfKings",
    "FrightNight",
    "IslandHopping",
    "GhostTown",
    "BroomLake",
    "ChequeredFlag",
    "La-La-Land",
    "TheMoneyPit",
    "ANationalTreasure",
    "MountainRescue",
    "NewBlood",
    "ParadiseIsland",
    "CosmicCraggs",
    "Gunslinger",
    "GoWithTheFlow",
];
const kDay = 90 / 7;
const kLevels = ["apprentice", "entrepreneur", "tycoon"];
// What a coaster goal's figure is, by its number.
const kFigures = [
    "excitement",
    "intensity",
    "nausea",
    "length",
    "neg G",
    "pos G",
    "lateral G",
    "speed",
    "height span",
    "air time",
    "inversions",
    "drop",
];

const picked = args().filter((a) => !a.startsWith("-"));
const lines: string[] = [];
let blocked = 0;
for (const name of picked.length > 0 ? picked : kScenarios) {
    await park.open(`Campaigns/Base/${name}.dat`, { sandbox: false });
    await park.ready();
    park.setDate(park.date() + kDay); // The goals are read as each day begins
    await park.advance(1);
    lines.push(`== ${name}`);
    const goals = park.goals().filter((g) => g.objective === 0 && g.challenge === null);
    for (let level = 0; level < kLevels.length; ++level) {
        const mine = goals.filter((g) => g.level === level);
        lines.push(`  ${kLevels[level]}: ${mine.length === 0 ? "no goals, never won" : ""}`);
        if (mine.length === 0) ++blocked;
        for (const g of mine) {
            const vip = g.kind === "VIPCondition";
            const coaster =
                g.figure !== null
                    ? ` ${g.rides} rides ${g.atMost ? "<=" : ">="} ${+g.target!.toFixed(2)} ${kFigures[g.figure] ?? g.figure}`
                    : "";
            const target = coaster !== "" ? coaster : g.target !== null ? ` ${g.atMost ? "<=" : ">="} ${g.target}` : "";
            const held = g.months > 0 ? ` for ${g.months} months` : "";
            const now = g.now !== null ? ` now ${g.now}` : "";
            const stuck = !g.judged
                ? "  NOT JUDGED"
                : g.now === null && !vip && g.kind !== "RepayLoanCondition"
                  ? "  NO FIGURE"
                  : "";
            if (stuck !== "") ++blocked;
            lines.push(`    ${g.kind}${target}${held}${now}${g.met ? " met" : ""}${stuck}`);
        }
    }
    for (const v of park.vipGoals())
        lines.push(
            `  VIP ${kLevels[v.level]}: celebrity ${v.who} wishes ${v.wishes} stays ${v.months} months, next ${Math.round(v.next - park.date())} units on${v.started ? ", visit under way" : ""}`,
        );
    // The first level's VIP is let in on their day, to show the stops they plan in the park as it opens.
    const first = park.vipGoals().find((v) => v.level === 0);
    if (first !== undefined) {
        park.setDate(Math.max(first.next, park.date()));
        await park.advance(5);
        for (const v of park.vips())
            lines.push(`  VIP in: ${v.name}, ${v.stops} stops, patience ${v.patience.toFixed(2)}`);
    }
}
lines.push(`levels or goals that cannot be won: ${blocked}`);
for (const line of lines) log(line);
report("base_goals.txt", lines.join("\n") + "\n");
expect(blocked === 0, "every level of the base scenarios can be judged");
quit(0);
