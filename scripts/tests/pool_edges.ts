// SPDX-License-Identifier: GPL-3.0-or-later
//
// Into the water and out again. In a park the game saved with swimmers in a pool with ladders, on a hot day,
// swimmers dive in off the edge and climb down its ladders, and climb out over the wall and up the ladders,
// each standing where they are while its motion carries them over: tick by tick nobody crossing the edge moves
// further than a walker would, a ladder's climber hardly moves along the ground, and up a ladder they end at the
// paving's height. Each way in and out is shown from the side as it starts, halfway and once done. Give it such a
// park:
//
//   ./nob script scripts/tests/pool_edges.ts -- --no-intro --no-audio --seed 1 --park PARK

export {};

const kTile = 4; ///< World units across a tile, as a person's col and row count them
const kTick = 0.1; ///< Park seconds run on between looks; drawing a frame runs the park on a little more
const kMostPaces = 2.5; ///< Chosen by eye: a step of a swimmer crossing the edge is at most this many of the others' middle step
const kMostRise = 0.15; ///< Chosen by eye: world units up or down they may go between looks

await until(() => park.loaded(), 1200, "the park");
await frames(10);
park.setWeather("sunny");
park.setTemperature(28);
park.setTime(0.5);
options.set("camera_mode", 1);

// Swimmers but those off a diving board, who fall into the water as they will.
type Seen = { name: string; col: number; row: number; up: number; in_water: boolean; edge?: string };
const swimmers = (): (Seen | undefined)[] =>
    people
        .list()
        .map((p) =>
            p.state === "swimming" && p.swimwear && p.col !== undefined && p.diving === undefined
                ? { name: p.name, col: p.col, row: p.row!, up: p.up!, in_water: p.in_water === true, edge: p.pool_edge }
                : undefined,
        );

// Spa pools, sat down in and stood up out of by their own motions, are left out.
const spas = new Set(
    map
        .objects()
        .filter((o) => o.symbol === "Jacuzzi")
        .map((o) => `${o.col},${o.row}`),
);
const in_spa = (p: Seen) => spas.has(`${Math.floor(p.col)},${Math.floor(p.row)}`);
// Tick by tick: each crossing of the edge, how it goes, and the largest step of any of them.
let last = swimmers();
const ladders = new Map<number, { col: number; row: number; up: number; edge: string }>();
const counts = new Map<string, number>();
let most_step = 0;
let most_rise = 0;
let ladder_travel = 0;
let ladder_out_rise = Infinity;
const jumps: string[] = [];
for (let t = 0; t < 2000; ++t) {
    await park.advance(kTick);
    const now = swimmers();
    // How far swimmers going on as usual went since the last look: the middle of their steps.
    const steps = now
        .flatMap((p, i) =>
            p !== undefined &&
            last[i] !== undefined &&
            last[i]!.name === p.name &&
            p.edge === undefined &&
            last[i]!.edge === undefined
                ? [Math.hypot(p.col - last[i]!.col, p.row - last[i]!.row) * kTile]
                : [],
        )
        .filter((d) => d > 0.001)
        .sort((a, b) => a - b);
    const pace = steps.length > 0 ? steps[Math.floor(steps.length / 2)] : 0;
    now.forEach((p, i) => {
        const was = last[i];
        if (p === undefined || was === undefined || was.name !== p.name) return;
        if (p.edge !== undefined && was.edge !== p.edge) counts.set(p.edge, (counts.get(p.edge) ?? 0) + 1);
        // Crossing the edge, or done crossing it now.
        if (
            (p.edge !== undefined || was.edge !== undefined || p.in_water !== was.in_water) &&
            !in_spa(p) &&
            !in_spa(was)
        ) {
            const step = Math.hypot(p.col - was.col, p.row - was.row) * kTile;
            const rise = Math.abs(p.up - was.up);
            most_step = Math.max(most_step, step);
            most_rise = Math.max(most_rise, rise);
            if (step > kMostPaces * pace + 0.01 || rise > kMostRise)
                jumps.push(
                    `${t}: ${p.name} ${was.edge ?? (was.in_water ? "water" : "paving")} -> ${p.edge ?? (p.in_water ? "water" : "paving")} step ${step.toFixed(2)} rise ${rise.toFixed(2)} pace ${pace.toFixed(2)} at ${p.col.toFixed(2)} ${p.row.toFixed(2)} ${p.up.toFixed(2)} from ${was.col.toFixed(2)} ${was.row.toFixed(2)} ${was.up.toFixed(2)}`,
                );
        }
        // Up or down a ladder: from where they began it, along the ground and up.
        if (p.edge === "ladder out" || p.edge === "ladder in") {
            if (!ladders.has(i)) ladders.set(i, { col: p.col, row: p.row, up: was.up, edge: p.edge });
            const from = ladders.get(i)!;
            ladder_travel = Math.max(ladder_travel, Math.hypot(p.col - from.col, p.row - from.row) * kTile);
        } else if (ladders.has(i)) {
            const from = ladders.get(i)!;
            if (from.edge === "ladder out") ladder_out_rise = Math.min(ladder_out_rise, p.up - from.up);
            ladders.delete(i);
        }
    });
    last = now;
}
log(
    "crossings",
    JSON.stringify([...counts]),
    "largest step",
    most_step.toFixed(3),
    "rise",
    most_rise.toFixed(3),
    "ladder travel",
    ladder_travel.toFixed(3),
    "ladder rise",
    ladder_out_rise.toFixed(3),
);
for (const j of jumps.slice(0, 20)) log("jump", j);
expect(jumps.length === 0, `no swimmer crossing the edge jumping, not ${jumps.length}`);
expect((counts.get("dive in") ?? 0) > 0 && (counts.get("climb out") ?? 0) > 0, "swimmers diving in and climbing out");
expect((counts.get("ladder out") ?? 0) > 0, "swimmers up a ladder");
// Along the ground no more than the ladder's climb takes them on to the paving.
expect(ladder_travel < 0.45, `a ladder's climber nearly still along the ground, not ${ladder_travel.toFixed(2)} units`);
expect(
    ladder_out_rise === Infinity || ladder_out_rise > 0.05,
    `up a ladder, out at the paving over the water, not ${ladder_out_rise.toFixed(2)} up`,
);

// Each way in and out from the side: as it starts, halfway and once done.
const shoot = async (i: number, name: string) => {
    const p = people.list()[i];
    await ui.click("bar/Top_Pause");
    const ground = (col: number, row: number) => (camera.look({ col, row }), camera.view().target[1]);
    const from = { col: p.col! + 0.5, row: p.row! + 0.4 };
    camera.look(
        { col: p.col!, row: p.row!, up: p.up! + 0.6 - ground(p.col!, p.row!) },
        { ...from, up: p.up! + 0.9 - ground(from.col, from.row) },
    );
    await frames(3);
    await screenshot(name);
    await ui.click("bar/Top_Pause");
};
for (const [edge, name] of [
    ["dive in", "dive"],
    ["climb out", "out"],
    ["ladder out", "ladder_out"],
    ["ladder in", "ladder_in"],
] as const) {
    const found = () => people.list().findIndex((p) => p.pool_edge === edge);
    try {
        await until(() => found() >= 0, 3000, `a swimmer to ${edge}`);
    } catch {
        log("none seen to", edge);
        continue;
    }
    const i = found();
    await shoot(i, `pool_edges_${name}_start`);
    await frames(40);
    await shoot(i, `pool_edges_${name}_mid`);
    await until(() => people.list()[i].pool_edge !== edge, 600, `the ${edge} done`);
    await shoot(i, `pool_edges_${name}_after`);
}
