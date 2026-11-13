// SPDX-License-Identifier: GPL-3.0-or-later
//
// A high diving board up on a raised deck, its boards out over a pool far below: in a park the game saved with
// one, swimmers climb it, dive off and fall all the way down into the low pool. Give it such a park:
//
//   ./nob script scripts/tests/pool_dives_raised.ts -- --no-intro --no-audio --seed 1 --park PARK

export {};

const kPool = "Pool/Default/Default_Stub";

await until(() => park.loaded(), 1200, "the park");
await frames(10);
park.setWeather("sunny");
park.setTemperature(28);
park.setTime(0.5);
options.set("camera_mode", 1);
// The highest tower, and the water under its boards' tip: the lowest water square beside it.
const bases = map.objects().filter((o) => o.overlay === kPool && o.symbol === "DBBase");
expect(bases.length > 0, "a high diving board in the park");
const base = bases.reduce((a, b) => (b.height! > a.height! ? b : a));
const water = (col: number, row: number) => map.objects().filter((o) => o.overlay === kPool && o.symbol === "Base" && o.col === col && o.row === row).map((o) => o.height!);
const below = [[0, 1], [1, 0], [0, -1], [-1, 0]].map(([dc, dr]) => ({ col: base.col + dc, row: base.row + dr, floor: Math.min(...water(base.col + dc, base.row + dr)) })).filter((w) => w.floor < base.height! - 4);
expect(below.length > 0, `a pool far below the tower at ${base.col}, ${base.row}, height ${base.height}`);
const low = below[0];
log("tower", base.col, base.row, "at", base.height, "over water at", low.col, low.row, "floor", low.floor);
// The way it faces, and across it.
const fc = low.col - base.col;
const fr = low.row - base.row;
camera.look({ col: base.col + 0.5, row: base.row + 0.5, up: 6 }, { col: base.col + 0.5 + 9 * fc - 5 * fr, row: base.row + 0.5 + 9 * fr + 5 * fc, up: 4 });
await frames(5);
await screenshot("pool_dives_raised_tower");

const near = (p: { col?: number; row?: number }) => Math.abs(p.col! - (base.col + 0.5)) < 2.5 && Math.abs(p.row! - (base.row + 0.5)) < 2.5;
const diver = (step: string) => people.list().find((p) => p.high_board === true && p.diving === step && near(p))?.name;
// By name: the list's order changes as guests come and go.
const who = (name: string) => people.list().find((p) => p.name === name)!;
const ground = (col: number, row: number) => (camera.look({ col, row }), camera.view().target[1]);
// From the side of the boards: level with the diver, or, given `mid`, far enough back to see from the boards down
// to the water, looking at that height.
const frame = (p: { col?: number; row?: number; up?: number }, mid?: number) => {
    const away = mid === undefined ? 3 : 9;
    const at = mid === undefined ? { col: p.col!, row: p.row!, up: p.up! + 0.9 } : { col: low.col + 0.5, row: low.row + 0.5, up: mid };
    const from = { col: at.col - away * fr, row: at.row + away * fc, up: mid === undefined ? p.up! + 1.2 : mid };
    camera.look({ ...at, up: at.up - ground(at.col, at.row) }, { ...from, up: from.up - ground(from.col, from.row) });
};
const look_at = async (named: string, name: string, mid?: number) => {
    await ui.click("bar/Top_Pause");
    const p = who(named);
    log(name, p.diving, "at", p.col?.toFixed(2), p.row?.toFixed(2), "up", p.up?.toFixed(2));
    frame(p, mid);
    await frames(3);
    await screenshot(name);
    await ui.click("bar/Top_Pause");
};

await until(() => diver("climb") !== undefined, 12000, "a swimmer climbing the raised tower");
const high = diver("climb")!;
await until(() => who(high).diving === "jump", 3600, "the diver at the board's end");
const top = who(high).up!;
await look_at(high, "pool_dives_raised_board");
// Back far enough to see the boards, the diver and the low pool, framed before the fall: people seen from a new
// distance are built again a few frames later, so a view moved there while paused shows the diver missing.
const mid = (top + ground(low.col + 0.5, low.row + 0.5)) / 2;
frame(who(high), mid);
await until(() => who(high).diving === "fall" && who(high).up! < top - 4, 600, "the diver falling");
await look_at(high, "pool_dives_raised_fall", mid);
await until(() => who(high).diving === "surface", 600, "the diver in the low pool");
const landed = who(high);
await look_at(high, "pool_dives_raised_splash");
log("fell from", top.toFixed(2), "to", landed.up?.toFixed(2), "at", landed.col?.toFixed(2), landed.row?.toFixed(2));
expect(water(Math.floor(landed.col!), Math.floor(landed.row!)).length > 0, `a water square where the diver lands, not ${landed.col?.toFixed(2)}, ${landed.row?.toFixed(2)}`);
expect(top - landed.up! > 10, `a long fall into the low pool, not ${(top - landed.up!).toFixed(2)}`);
// Out past the tower's wall, the way it faces: the low pool, not the deck.
const out = (landed.col! - (base.col + 0.5)) * (low.col - base.col) + (landed.row! - (base.row + 0.5)) * (low.row - base.row);
expect(out > 0.5 && out < 6, `in the low pool in front of the tower, not ${out.toFixed(2)} out`);
await until(() => who(high).diving === undefined, 600, "the diver swimming on");
expect(who(high).state === "swimming", "the diver swimming on");
