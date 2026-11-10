// SPDX-License-Identifier: GPL-3.0-or-later
//
// Rings on the water where swimmers fall in. In a park the game saved with a high diving board in a pool and guests
// swimming, a diver off its end goes under with a little spray, and rings spread out over the pool from there,
// lower as they go, until they are gone; a swimmer diving in off the pool's edge sends out smaller ones. Give it
// such a park:
//
//   ./nob script scripts/tests/pool_ripples.ts -- --no-intro --no-audio --seed 1 --park PARK

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

await until(() => park.loaded(), 1200, "the park");
await frames(10);
park.setWeather("sunny");
park.setTemperature(28);
park.setTime(0.5);
options.set("camera_mode", 1);

// By name: the list's order changes as guests come and go.
const who = (name: string) => people.list().find((p) => p.name === name)!;
const ground = (col: number, row: number) => (camera.look({ col, row }), camera.view().target[1]);
const near = (r: { col: number; row: number }, p: { col?: number; row?: number }) => Math.hypot(r.col - p.col!, r.row - p.row!) < 0.5;
// Paused, so the rings hold still for the pictures: close and low over the water, then from straight above.
const shots = async (p: { col?: number; row?: number }, name: string) => {
    await ui.click("bar/Top_Pause");
    const g = ground(p.col!, p.row!);
    const water = map.rings().find((r) => near(r, p))?.up ?? g;
    camera.look({ col: p.col!, row: p.row!, up: water - g }, { col: p.col! + 1.2, row: p.row! + 1.2, up: water - g + 3 });
    await frames(3);
    await screenshot(`${name}_close`);
    camera.look({ col: p.col!, row: p.row! }, { col: p.col!, row: p.row! + 0.05, up: water - g + 9 });
    await frames(3);
    await screenshot(`${name}_above`);
    await ui.click("bar/Top_Pause");
};

// Off the high board: rings where the diver goes under, wider and lower a second on, gone a few seconds after.
await until(() => people.list().some((p) => p.high_board === true && p.diving === "fall"), 9000, "a swimmer diving off the high board");
const diver = people.list().find((p) => p.high_board === true && p.diving === "fall")!.name;
await until(() => who(diver).diving === "surface", 600, "the diver in the water");
const entry = who(diver);
const rings = () => map.rings().find((r) => near(r, entry));
await frames(2);
const first = rings();
log("rings", JSON.stringify(map.rings()));
expect(first !== undefined, `rings where the diver went under, at ${entry.col?.toFixed(2)}, ${entry.row?.toFixed(2)}`);
await shots(entry, "pool_ripples_dive");
await until(() => (rings()?.age ?? 0) > 1, 300, "the rings a second on");
const later = rings()!;
expect(later.crest < first!.crest, `the rings lower as they spread, ${later.crest} after ${first!.crest}`);
await shots(entry, "pool_ripples_dive_later");
// Little spray: none of the big splashes.
expect(!map.effects().some((e) => e.effect === "watersplashbig"), "no big splash over the diver");
await until(() => rings() === undefined, 900, "the rings dying out");

// In off the edge: a swimmer on the paving who is in the water a moment later, not off a board.
const dry = new Set(people.list().filter((p) => p.state === "swimming" && p.in_water === false && p.diving === undefined).map((p) => p.name));
let jumper: string | undefined;
await until(() => (jumper = people.list().find((p) => dry.has(p.name) && p.in_water === true && p.diving === undefined)?.name) !== undefined, 9000, "a swimmer diving in off the edge");
const dived = who(jumper!);
await until(() => map.rings().some((r) => near(r, dived) && r.age > 0.6), 300, "rings where they dived in");
const edge = map.rings().find((r) => near(r, dived))!;
expect(edge.crest < first!.crest, `smaller rings off the edge, ${edge.crest} against ${first!.crest}`);
await shots(dived, "pool_ripples_edge");
await until(() => !map.rings().some((r) => near(r, edge)), 900, "the edge rings dying out");
