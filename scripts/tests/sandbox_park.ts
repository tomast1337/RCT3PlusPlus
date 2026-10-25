// SPDX-License-Identifier: GPL-3.0-or-later
//
// Builds a park on the game's blank landscape the way a player would, and checks each part as it goes in:
// for now a grid of paths laid from point to point with the path panel's route switch, and a path off its
// side that climbs two levels by a slope and stairs, runs along raised and comes down again. Meant to grow into
// one park that holds every stall, ride and coaster.
//
//   ./nob script scripts/tests/sandbox_park.ts

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

/** Where the middle of tile (col, row) is on the view; throws when it is out of sight. */
function on_view(col: number, row: number): [number, number] {
    const at = map.screen(col, row);
    if (!at)
        throw new Error(`tile ${col}, ${row} is not in sight`);
    return [at.x, at.y];
}

// The grid: `lines` paths each way, `gap` tiles apart, round the middle of the map.
const lines = 4;
const gap = 4;
const span = (lines - 1) * gap;

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined, 300, "the tool row");

// A sandbox lets the player choose the day's mode: the bar's button opens the modes, and a pick closes them.
/** Picks the day mode `mode` from the bar. */
async function day_mode(mode: string): Promise<void> {
    await ui.click("bar/day_mode");
    await until(() => ui.find(`bar/day_mode/${mode}`) !== undefined, 30, "the day modes");
    await ui.click(`bar/day_mode/${mode}`);
    await until(() => ui.find(`bar/day_mode/${mode}`) === undefined, 30, "the day modes to close");
}
await until(() => ui.find("bar/day_mode") !== undefined, 30, "a day mode button in a sandbox");
await day_mode("night");
await frames(300);
await screenshot("night");
await day_mode("park_hours");
const { width, height } = map.size();
const c0 = (width - span) >> 1;
const r0 = (height - span) >> 1;
for (let r = r0; r <= r0 + span; ++r)
    for (let c = c0; c <= c0 + span; ++c)
        expect(map.tile(c, r)!.owned, `tile ${c}, ${r} to be the park's`);

const mid = { col: c0 + span / 2 + 0.5, row: r0 + span / 2 + 0.5 };
camera.look(mid, { col: mid.col, row: mid.row - 14, up: 70 });
await frames(2);
await screenshot("empty");
const before = map.paths().length; // The landscape's own, by its entrance

await ui.click("tools/Paths/Obj_Paths");
await until(() => ui.find("footpaths/route") !== undefined, 120, "the path panel");
// The camera stays where the script puts it.
await ui.toggle("footpaths/follow", false);

/** Lays a path from tile `a` to tile `b`: a click on `a` starts it, then the route goes on to `b`. */
async function route(a: [number, number], b: [number, number]): Promise<void> {
    await ui.toggle("footpaths/route", false);
    await pointer.click(...on_view(a[0], a[1]));
    await ui.toggle("footpaths/route", true);
    // The route shows once the pointer rests on the goal; the click lays it.
    pointer.move(...on_view(b[0], b[1]));
    await frames(3);
    await pointer.click(...on_view(b[0], b[1]));
}

// Across first, then down between them, so each line starts on bare ground.
for (let i = 0; i < lines; ++i)
    await route([c0, r0 + i * gap], [c0 + span, r0 + i * gap]);
for (let i = 0; i < lines; ++i)
    await route([c0 + i * gap, r0 + 1], [c0 + i * gap, r0 + span - 1]);
await ui.toggle("footpaths/route", false);
await frames(5);
await screenshot("paths");

const laid = new Set(map.paths().map((p) => `${p.col},${p.row}`));
const missing: string[] = [];
for (let r = r0; r <= r0 + span; ++r)
    for (let c = c0; c <= c0 + span; ++c)
        if (((r - r0) % gap === 0 || (c - c0) % gap === 0) && !laid.has(`${c},${r}`))
            missing.push(`${c},${r}`);
const extra = map.paths().length - before - (2 * lines * (span + 1) - lines * lines);
log("paths", map.paths().length, "missing", missing.length, "beyond the grid", extra);
expect(missing.length === 0, `a path on every grid tile; missing ${missing.join(" ")}`);
expect(extra === 0, "no paths off the grid");
expect(map.paths().every((p) => !p.raised), "every path on the ground");

// The climb: a stroke east off the grid's second line sets the way, then the controls lay it piece by piece.
const climb_row = r0 + gap;
const ground = map.paths().length;
await pointer.drag(on_view(c0 + span + 1, climb_row), on_view(c0 + span + 2, climb_row), 10);
/** Picks the slope `control`, then lays `count` pieces straight on. */
async function lay(control: string, count: number): Promise<void> {
    await ui.click(`footpaths/body/${control}`);
    for (let i = 0; i < count; ++i)
        await ui.click("footpaths/body/Path_Straight");
}
await lay("Path_UpwardSlope", 1);
await lay("Path_UpwardSteps", 1);
await lay("Path_NoSlope", 3);
const top = Math.max(...map.paths().map((p) => p.height ?? 0));
await lay("Path_DownwardSteps", 1);
await lay("Path_DownwardSlope", 1);
await lay("Path_NoSlope", 1);
camera.look({ col: c0 + span + 5, row: climb_row + 0.5 }, { col: c0 + span - 3, row: climb_row + 10, up: 25 });
await frames(5);
await screenshot("climb");

const pieces = map.paths().slice(ground);
const raised = pieces.filter((p) => p.raised);
log("climb pieces", pieces.length, "raised", raised.length, "heights", JSON.stringify(raised.map((p) => p.height)), "top", top);
expect(pieces.length === 2 + 1 + 1 + 3 + 1 + 1 + 1, "every piece of the climb laid");
expect(raised.length > 0 && top > 0, "the climb to leave the ground");
expect(!pieces[pieces.length - 1].raised, "the climb to come back down to the ground");
