// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every Wild! scenario's goals, played as a scenario: each goal and challenge goal is one the simulation can
// judge, and after a day the park's reading on it is printed. The goals on a species name one the park knows.
//
//   ./nob script scripts/tests/wild_goals.ts -- --no-intro --no-audio --seed 1

export {};

const kDay = 90 / 7;
const kParks = [
    "GeminiBasin",
    "GolfCourse",
    "InsectWorld",
    "Jungle",
    "LostLandOfTheDinosaurs",
    "MineMountain",
    "OstrichFarm",
    "Pyramids",
    "RaidersOfTheLostCoaster",
    "RockyCoasters",
    "RunDownZoo",
    "ScrubGardens",
];
const kSpeciesGoals = [
    "NumAnimalsOfSpeciesCondition",
    "AnimalSalesProfitCondition",
    "ReleaseAnimalsCondition",
];

for (const name of kParks) {
    await park.open(`Campaigns/Base/Wild/${name}.dat`, { sandbox: false });
    await park.ready();
    await frames(10);
    // The goals are read as each day begins.
    park.setDate(park.date() + kDay);
    await frames(10);
    for (const g of park.goals()) {
        log(
            name,
            `level ${g.level} ${g.kind}${g.challenge === null ? "" : ` (challenge ${g.challenge})`}${g.species ? ` ${g.species}` : ""}: ${g.now ?? "-"} of ${g.target ?? "-"}${g.met ? ", met" : ""}`,
        );
        expect(g.judged, `${name}: ${g.kind} judged`);
        if (kSpeciesGoals.includes(g.kind)) expect(g.species !== "", `${name}: ${g.kind} names its species`);
    }
}
