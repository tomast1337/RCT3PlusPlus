// SPDX-License-Identifier: GPL-3.0-or-later
//
// Hired boats out on a park's water: each frame a boat moves on and turns a little, never jumping from one
// step of the simulation to the next; boats of one ride keep clear of each other (bumper boats bump, but never
// sink into one another); and every boat keeps to the water. Rowing boats row with both oars, or with those that
// turn them the way they turn. A boat is pictured out on the water, a rowing boat close up mid-stroke. Give it a
// park the game saved with a boat hire ride (rowing boats, swans, canoes, bumper boats, jet skis and the like):
//
//   ./nob script scripts/tests/boats.ts -- --no-intro --no-audio --seed 1 --park PARK

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

const kBoats = /rowingboat|swanboat|canoe|bumperboat|jetski|windsurf|watertricycle/i;
const kFrames = 60 * 20; // Frames watched, a sixtieth of a second each
const kMostTurn = 0.06; // Radians a boat may turn in a frame: the quickest boats turn some 2 a second
const kMostChange = 0.02; // Metres a boat's step may change by from one frame to the next

await until(() => park.loaded(), 1200, "the park");
await frames(10);
const hires = rides.list().map((r, i) => ({ r, i })).filter(({ r }) => kBoats.test(r.symbol) && (r.cars?.length ?? 0) > 0);
expect(hires.length > 0, "a boat hire ride in the park");
log("boat rides", hires.map(({ r }) => `${r.symbol} (${r.cars!.length} boats)`).join(", "));
const bumping = (i: number) => /bumperboat/i.test(rides.list()[i].symbol);
const cars = (i: number) => rides.list()[i].cars!;
const tile = (c: { col: number; row: number }) => map.tile(Math.floor(c.col), Math.floor(c.row));
const water_at = (c: { col: number; row: number }) => tile(c)?.water !== undefined;
const docked = hires.map(({ i }) => cars(i));
// Out on the water: the first boat to leave its dock.
await ui.click("bar/Top_FastForward");
await until(() => hires.some(({ i }, k) => cars(i).some((c, b) => Math.hypot(c.col - docked[k][b].col, c.row - docked[k][b].row) > 2)), 60 * 600, "a boat out on the water");
await ui.click("bar/Top_Play");

const kTile = 4; // Metres a tile
let worst_turn = 0;
let worst_change = 0;
let closest = Infinity;
let off_water = 0;
let moving = 0;
const strokes = new Map<string, { frames: number; turned: number }>(); // By stroke: frames rowed, radians turned meanwhile
const last = hires.map(({ i }) => cars(i));
const last_step: ({ x: number; z: number } | undefined)[][] = hires.map(({ i }) => cars(i).map(() => undefined));
for (let n = 0; n < kFrames; ++n) {
    await frames(1);
    for (const [k, { i }] of hires.entries()) {
        const now = cars(i);
        for (const [b, c] of now.entries()) {
            const was = last[k][b];
            const step = { x: (c.col - was.col) * kTile, z: (c.row - was.row) * kTile };
            if (Math.hypot(step.x, step.z) > 1 || Math.hypot(c.col - docked[k][b].col, c.row - docked[k][b].row) < 1)
                continue; // Leaving or back at its dock
            ++moving;
            const turned = Math.atan2(Math.sin(c.facing - was.facing), Math.cos(c.facing - was.facing));
            const turn = Math.abs(turned);
            if (c.stroke) {
                const rowed = strokes.get(c.stroke) ?? { frames: 0, turned: 0 };
                strokes.set(c.stroke, { frames: rowed.frames + 1, turned: rowed.turned + turned });
            }
            worst_turn = Math.max(worst_turn, turn);
            const before = last_step[k][b];
            if (before)
                worst_change = Math.max(worst_change, Math.hypot(step.x - before.x, step.z - before.z));
            last_step[k][b] = step;
            off_water += water_at(c) ? 0 : 1;
            for (const d of now.slice(b + 1))
                if (Math.hypot(d.col - docked[k][now.indexOf(d)].col, d.row - docked[k][now.indexOf(d)].row) > 1 && !bumping(i))
                    closest = Math.min(closest, Math.hypot(d.col - c.col, d.row - c.row) * kTile);
        }
        last[k] = now;
    }
}
log("boat frames", moving, "most turn a frame", worst_turn.toFixed(3), "most change of step", worst_change.toFixed(3), "closest apart", closest.toFixed(2), "off water", off_water);
expect(moving > 60, "boats moving on the water");
expect(worst_turn < kMostTurn, `no boat turning more than ${kMostTurn} in a frame, not ${worst_turn.toFixed(3)}`);
expect(worst_change < kMostChange, `no boat's step jumping by ${kMostChange} m or more, not ${worst_change.toFixed(3)}`);
expect(closest > 1.5, `boats keeping clear of each other, not ${closest.toFixed(2)} m apart`);
expect(off_water < moving / 50, `boats keeping to the water, ${off_water} frames off it`);

// Rowing, turning to its right (the way a boat's +X points, so facing grows) pulls the oars that turn it so, and
// to its left the others. A stroke is rowed to its end before the next, so this holds over many, not each frame.
if (strokes.size > 0) {
    log("strokes", [...strokes].map(([k, v]) => `${k} ${v.frames} frames turning ${v.turned.toFixed(2)}`).join(", "));
    expect((strokes.get("both")?.frames ?? 0) > 0, "boats rowing with both oars");
    expect((strokes.get("left")?.turned ?? 0) <= 0 && (strokes.get("right")?.turned ?? 0) >= 0, "boats rowing with the oars that turn them the way they turn");
    park.setTime(0.5);
    await frames(2);
    const rowing = hires.flatMap(({ i }) => cars(i).filter((c) => c.stroke))[0];
    if (rowing) {
        camera.look(rowing, { col: rowing.col + 0.4, row: rowing.row + 0.6, up: (rowing.up ?? 0) + 8 });
        await frames(3);
        await screenshot("rowing_oars");
    }
}

// Pictured out on the water.
const out = hires.flatMap(({ i }, k) => cars(i).filter((c, b) => Math.hypot(c.col - docked[k][b].col, c.row - docked[k][b].row) > 2))[0];
if (out) {
    camera.look({ col: out.col, row: out.row }, { col: out.col + 2, row: out.row + 3, up: 5 });
    await frames(30);
    await screenshot("boats_out");
}
