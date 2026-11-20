// SPDX-License-Identifier: GPL-3.0-or-later
//
// Zoo animals breeding in a Wild! scenario: the Debug tab makes the adult females pregnant, the clock runs on to
// the births, and each mother's young stand beside her, named, with a message for each birth. Screenshots: a
// mother and her young close up, with the birth messages at the top.
//
//   ./nob script scripts/tests/zoo_breeding.ts -- --no-intro --no-audio --seed 1

export {};

await park.open("Campaigns/Base/Wild/ScrubGardens.dat");
await park.ready();
await frames(10);

const before = animals.list();
log("animals", JSON.stringify(before));
const females = before.filter((a) => a.adult && !a.male && !a.crated);
expect(females.length > 0, "an adult female in the park");

expect(debug.use("breed_animals", 20) === females.length, "every adult female made pregnant");
expect(
    animals.list().every((a) => a.pregnant === (a.adult && !a.male && !a.crated)),
    "only the adult females pregnant",
);
park.advance(25);
await frames(5);

const after = animals.list();
const young = after.filter((a) => a.mother >= 0);
log("young", JSON.stringify(young));
expect(young.length === females.length, `one young for each mother, not ${young.length}`);
expect(after.length === before.length + young.length, "the young added to the park's animals");
for (const y of young) {
    const mother = must(
        after.find((a) => a.id === y.mother),
        "the young one's mother",
    );
    expect(!y.adult && !y.crated, `${y.name} born young and out of any crate`);
    expect(y.species === mother.species && y.enclosure === mother.enclosure, `${y.name} beside its mother`);
    expect(y.name.split(" ").length >= 2, `${y.name}: a name of its own`);
    expect(!mother.pregnant, `${mother.name} no longer pregnant`);
}
const births = park.messages().filter((m) => m.key === "WildAnimalMessage_GiveBirth");
expect(births.length === young.length, `a message for each birth, not ${births.length}`);

// A mother and her young, close up.
const first = young[0];
camera.look({ col: first.at.col, row: first.at.row, up: 1 }, { col: first.at.col + 2.4, row: first.at.row + 1.8, up: 2.5 });
await frames(10);
await screenshot("newborn");

quit();
