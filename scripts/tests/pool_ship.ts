// SPDX-License-Identifier: GPL-3.0-or-later
//
// An inflatable ship goes in beside a pool, and swimmers climb it and slide down into the water. In a park with
// pools and swimmers, a first ship goes on open grass, opened, to learn where the foot of its steps lies from where
// it stands. A second goes beside a pool, turned the same way, so the foot of its steps is in the pool. Opened,
// swimmers come over, climb its steps and slide down it, and its customers count up.
//
//   ./nob script scripts/tests/pool_ship.ts -- --no-intro --no-audio --seed 1 --park "<Parks>/pool-9-two-pools.dat"

export {};

const kOverlay = "tracks/TrackedRides/SinkingShipSlide/SinkingShip";
const isShip = (r: RideInfo) => /sinkingship/i.test(r.symbol ?? "");

await until(() => park.loaded(), 2400, "the park");
await until(() => build.flatRides() !== undefined, 1200, "the build lists");
if (park.cash() !== undefined) park.setCash(100000000);
const pool = map.poolPieces();
const water = pool.filter((o) => o.symbol === "Base");
expect(water.length > 0, "a park with pools");
const complex = new Set(pool.map((o) => `${o.col},${o.row}`));

/** Puts a ship down at the first of `spots` it fits, from above each, `exact`ly there or near; the spot it took. */
async function place(
    spots: { col: number; row: number }[],
    exact = false,
): Promise<{ col: number; row: number } | undefined> {
    expect(build.pick(kOverlay, "SinkingShip"), "the inflatable ship to pick");
    await frames(5);
    for (const spot of spots) {
        camera.look({ col: spot.col + 0.5, row: spot.row + 0.5 }, { col: spot.col + 0.5, row: spot.row + 14, up: 30 });
        await frames(3);
        const at = map.screen(spot.col + 0.5, spot.row + 0.5);
        if (!at) continue;
        pointer.move(at.x, at.y);
        await frames(2);
        const c = build.construction();
        if (
            c &&
            c.ghost &&
            !c.clashes &&
            c.ghostAt &&
            (!exact || (c.ghostAt.col === spot.col && c.ghostAt.row === spot.row))
        ) {
            const took = { col: c.ghostAt.col, row: c.ghostAt.row };
            await pointer.click(at.x, at.y);
            await frames(10);
            return took;
        }
    }
    return undefined;
}

/** Opens ship `i` and waits for the park to run it. */
async function open(i: number): Promise<RideInfo> {
    const refused = rides.setState(i, "open");
    expect(refused == null, `ship ${i} to open, not refused for ${refused}`);
    await until(() => rides.list()[i].climbWays !== undefined, 300, "the park to run the ship");
    return rides.list()[i];
}

// 1. A first ship on open grass, well away from the pools, to learn where the foot of its steps lies.
const minCol = Math.min(...water.map((o) => o.col));
const maxRow = Math.max(...water.map((o) => o.row));
const grass: { col: number; row: number }[] = [];
for (let row = maxRow + 10; row < maxRow + 20; row += 3)
    for (let col = minCol - 10; col < minCol + 30; col += 3) grass.push({ col, row });
const first = await place(grass);
expect(first !== undefined, "the first ship to go on open grass");
const firstShip = rides.list().findIndex(isShip);
expect(firstShip >= 0, "the first ship among the rides");
const opened = await open(firstShip);
expect(opened.climbWays === 2, `two ways down the ship, not ${opened.climbWays}`);
const foot = opened.climbFoot!;
const dc = Math.floor(foot.col) - first!.col;
const dr = Math.floor(foot.row) - first!.row;
log(`steps' foot ${dc}, ${dr} from where the ship stands`);

// 2. A second beside a pool, standing where the foot of its steps falls in the water.
const shore = [...complex]
    .map((k) => k.split(",").map(Number))
    .map(([col, row]) => ({ col: col - dc, row: row - dr }))
    .filter((s) => !complex.has(`${s.col},${s.row}`));
const second = await place(shore, true);
expect(second !== undefined, "a ship to go beside a pool");
await screenshot("pool_ship_placed");
expect(map.objects().filter((o) => o.symbol === "SinkingShip").length >= 2, "both ships drawn");
const ship = rides
    .list()
    .map((r, i) => (isShip(r) ? i : -1))
    .reduce((a, b) => Math.max(a, b), -1);
expect(ship !== firstShip, "the second ship among the rides");
const running = await open(ship);
expect(
    complex.has(`${Math.floor(running.climbFoot!.col)},${Math.floor(running.climbFoot!.row)}`),
    "the second ship's steps to start in the pool",
);

// 3. Swimmers come over, climb up and slide down; its customers count up over time.
let climbed = 0;
let slid = 0;
let shot = false;
const counts: number[] = [];
for (let minute = 0; minute < 12 && (counts.at(-1) ?? 0) < 3; ++minute) {
    // A look each second: a slide down lasts only a few.
    for (let s = 0; s < 60; ++s) {
        await park.advance(1);
        const on = people.list().filter((p) => p.ride === ship && p.climbing !== undefined);
        climbed += on.filter((p) => p.climbing === "climbing").length;
        slid += on.filter((p) => p.climbing === "sliding").length;
        if (!shot && on.some((p) => p.climbing === "sliding")) {
            const p = must(
                on.find((q) => q.climbing === "sliding"),
                "someone sliding down the ship",
            );
            camera.look({ col: p.col!, row: p.row!, up: p.up! }, { col: p.col! + 3, row: p.row! + 4, up: p.up! + 3 });
            await frames(5);
            await screenshot("pool_ship_sliding");
            shot = true;
        }
    }
    counts.push(rides.list()[ship].customers ?? 0);
}
log(`customers by the minute ${counts.join(" ")}, seen climbing ${climbed}, sliding ${slid}`);
expect((counts.at(-1) ?? 0) >= 2, `swimmers on the ship, not ${counts.at(-1)}`);
expect(climbed > 0 && slid > 0, "swimmers seen both climbing and sliding");
expect(rides.list()[firstShip].customers === 0, "nobody on the ship away from the pools");

// 4. Its window.
rides.show(ship);
await frames(10);
await screenshot("pool_ship_window");
