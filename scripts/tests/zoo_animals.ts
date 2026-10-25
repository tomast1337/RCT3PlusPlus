// SPDX-License-Identifier: GPL-3.0-or-later
//
// Zoo animals: a Wild! scenario's saved animals, simulated and drawn with their own models, seen close up.
//
//   ./nob script scripts/tests/zoo_animals.ts -- --no-intro --no-audio --no-simulate --seed 1

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

/** The park's animals, each where it stands. */
function animals_list(): { species: string; col: number; row: number }[] {
    return animals.list().map((a) => ({ species: a.species, col: a.at.col, row: a.at.row }));
}

await park.open("Campaigns/Base/Wild/ScrubGardens.dat");
await until(() => ui.find("tools/Land/Land") !== undefined, 300, "the tool row");

const zoo = animals_list();
log("animals", JSON.stringify(animals.list()));
expect(zoo.length > 0, "the scenario's animals");

// Close up, from a couple of tiles off and a little above: the first animal, and one of each other species.
const close = zoo.filter((a, i) => zoo.findIndex((b) => b.species.toLowerCase() === a.species.toLowerCase()) === i);
for (const [i, a] of close.entries()) {
    camera.look({ col: a.col, row: a.row, up: 1.5 }, { col: a.col + 2.4, row: a.row + 1.8, up: 3 });
    await frames(10);
    await screenshot(`close_${i}_${a.species}`);
}
await frames(20);
await screenshot("close_later");

// Every animal at once, from higher up.
const col = zoo.reduce((s, a) => s + a.col, 0) / zoo.length;
const row = zoo.reduce((s, a) => s + a.row, 0) / zoo.length;
camera.look({ col, row }, { col: col + 12, row: row + 12, up: 14 });
await frames(10);
await screenshot("all");
