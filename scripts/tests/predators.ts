// SPDX-License-Identifier: GPL-3.0-or-later
//
// Predators and prey: a lion bought into a run-down zoo's zebra enclosure grows hungry, runs the zebra down and kills
// it. The park posts a message, and the body lies where it fell. Screenshots: the chase, and the body afterwards.
//
//   ./nob script scripts/tests/predators.ts -- --no-intro --no-audio --seed 1

export {};

const kKilled = "WildAnimalMessage_Killed";

await park.open("Campaigns/Base/Wild/RunDownZoo.dat");
await park.ready();
await until(() => ui.find("tools/Land/Land") !== undefined, 300, "the tool row");

const zebra = must(
    animals.list().find((a) => a.species.toLowerCase() === "zebra" && a.enclosure >= 0 && !a.crated),
    "a zebra in an enclosure",
);
log("zebra", JSON.stringify(zebra));
const col = Math.floor(zebra.at.col);
const row = Math.floor(zebra.at.row);
await look_down(col + 0.5, row + 0.5);
const had = new Set(animals.list().map((a) => a.id));
expect(build.pick("WildAnimals/WildAnimals", "lion", "adult male"), "the adult male lion in the animal list");
await pointer.click(...on_view(col + 0.5, row + 0.5));
await frames(3);
build.stop();
const lion = must(
    animals.list().find((a) => !had.has(a.id)),
    "the lion bought",
);
log("lion", JSON.stringify(lion));
expect(
    lion.enclosure === zebra.enclosure,
    `the lion in the zebra's enclosure ${zebra.enclosure}, not ${lion.enclosure}`,
);

const now = (id: number) => animals.list().find((a) => a.id === id);
// Out of its crate, the lion grows hungry and goes after the prey of its enclosure: the zebra or the horses.
await park.advanceUntil(() => (now(lion.id)?.hunting ?? -1) >= 0, 900, "the lion hunting");
const quarry = must(now(must(now(lion.id), "the lion").hunting), "the lion's quarry");
log("hunting", JSON.stringify(quarry), "hunger", now(lion.id)?.hunger);
await frames(20);
log("lion", JSON.stringify(now(lion.id)), "quarry", JSON.stringify(now(quarry.id)));
const a = must(now(lion.id), "the lion");
const b = now(quarry.id) ?? quarry;
const mid = { col: (a.at.col + b.at.col) / 2, row: (a.at.row + b.at.row) / 2 };
camera.look({ col: mid.col, row: mid.row, up: 0.5 }, { col: mid.col - 3, row: mid.row - 1.5, up: 3 });
await frames(5);
await screenshot("predators_chase");

await park.advanceUntil(() => now(quarry.id) === undefined, 120, "the quarry caught");
const told = park.messages().filter((m) => m.key === kKilled);
log("messages", JSON.stringify(told));
expect(
    told.some((m) => m.name === quarry.name),
    `a message that ${quarry.name} was killed`,
);
const bodies = animals.carcasses();
log("carcasses", JSON.stringify(bodies));
expect(bodies.length === 1, `one body left, not ${bodies.length}`);
expect((now(lion.id)?.hunger ?? 1) < 0.2, "the lion fed");
camera.look(
    { col: bodies[0].col, row: bodies[0].row, up: 0.5 },
    { col: bodies[0].col - 2, row: bodies[0].row - 1, up: 3 },
);
await frames(10);
await screenshot("predators_body");

quit();
