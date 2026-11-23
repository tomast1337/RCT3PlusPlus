// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every Soaked! scenario's goals, its challenges' too, read as the first day's check finds them: each prints its
// level, target and where the park stands, and none may be one the simulation cannot judge, or its level could
// never be won.
//
//   ./nob script scripts/tests/soaked_goals.ts -- --no-audio

export {};

const kDay = 90 / 7;
const kScenarios = [
    "AquaMountain",
    "Atlantis",
    "CastawayGetaway",
    "CptBlackheartsCove",
    "FountainOfYouth",
    "MonsterLake",
    "OasisOfFun",
    "TreasureIsland",
    "WorldOfTheSea",
];

for (const name of kScenarios) {
    await park.open(`Campaigns/Base/Soaked/${name}.dat`);
    await park.ready();
    await until(() => ui.find("tools/Management") !== undefined, 1200, "the tool row");
    park.setDate(park.date() + kDay);
    await frames(30);
    const goals = park.goals();
    expect(goals.length > 0, `${name} sets goals`);
    for (const g of goals)
        log(
            name,
            `level ${g.level}`,
            g.challenge !== null ? `challenge ${g.challenge}` : "scenario",
            g.kind,
            `target ${g.target}`,
            `now ${g.now}`,
            g.met ? "met" : "unmet",
            g.judged ? "judged" : "UNJUDGED",
        );
    expect(
        goals.every((g) => g.judged),
        `every goal of ${name} judged`,
    );
}
quit(0);
