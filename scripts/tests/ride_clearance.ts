// SPDX-License-Identifier: GPL-3.0-or-later
//
// The room coaster track keeps clear for its riders, on the game's blank landscape. A path is raised over a tile,
// and a corkscrew coaster's first piece, its station, is lifted under it: it may stand only with a quarter of its
// height more between its top and the path than its levels alone would leave, and over the path as before. Then a
// custom track dives into the ground from its station: the ground is cut round it until there is a unit of ground
// over the riders' room, then it runs on in a tunnel.
//
//   ./nob script scripts/tests/ride_clearance.ts -- --no-audio

export {};

/** Where the middle of tile (col, row) is on the view; throws when it is out of sight. */

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "tools");
const gate = map.paths().reduce((a, b) => (b.row > a.row ? b : a));
const col = gate.col + 6;
const row = gate.row - 12;
camera.look({ col: col + 2.5, row: row + 0.5 }, { col: col + 2.5, row: row + 12, up: 16 });
await frames(5);

// A path raised by three flights of stairs, then level: a tile of its level part is the one under test.
await ui.click("tools/Paths/Obj_Paths");
await until(() => ui.find("footpaths/route") !== undefined, 120, "the path panel");
await pointer.drag(on_view(col - 3, row), on_view(col - 2, row), 10);
async function lay(control: string, count: number): Promise<void> {
    await ui.click(`footpaths/body/${control}`);
    for (let i = 0; i < count; ++i)
        await ui.click("footpaths/body/Path_Straight");
}
await lay("Path_UpwardSteps", 3);
await lay("Path_NoSlope", 3);
await ui.click("tools/Paths/Obj_Paths"); // Put away
await frames(5);
const top = Math.max(...map.paths().filter((p) => p.row === row && p.raised).map((p) => p.height ?? 0));
const over = map.paths().find((p) => p.row === row && p.raised && p.height === top && p.col > col);
log("paths", JSON.stringify(map.paths().filter((p) => p.raised)));
expect(over !== undefined && (over.height ?? 0) >= 5, "a path raised at least five units over the tile under test");
const deck = over!.height!;

// The station lifted on the tile under the path, a step at a time: where it may stand.
async function custom(): Promise<number> {
    expect(build.pick("tracks/coasters/Corkscrew/Corkscrew", "Corkscrew", "designs"), "a corkscrew coaster to build");
    await until(() => ui.find("design_browser") !== undefined, 600, "the designs window");
    await frames(3);
    await ui.click("design_browser/content/build");
    await frames(10);
    pointer.move(0.5, 0.5);
    await frames(5);
    expect(/station/i.test(build.construction()?.piece ?? ""), "a station picked to begin with");
    return build.construction()!.ride;
}
await custom();
const [x, y] = on_view(over!.col + 0.5, row + 0.5);
pointer.move(x, y);
await frames(10);
const start = build.construction()!.ghostAt!;
log("station ghost at", JSON.stringify(start));
pointer.shift(true);
await frames(3);
const free = new Map<number, boolean>();
const lifts = new Map<number, number>(); // Pointer height on the view for each station height
for (let i = 1; i <= 60 && Math.max(-Infinity, ...free.keys()) < deck + 4; ++i) {
    pointer.move(x, y - 0.008 * i);
    await frames(5);
    const c = build.construction()!;
    if (!c.ghostAt || c.ghostAt.col !== start.col || c.ghostAt.row !== start.row)
        continue;
    if (c.ghostAt.height === deck - 3 && !free.has(deck - 3))
        await screenshot("station_under_path_refused");
    free.set(c.ghostAt.height, !c.clashes);
    lifts.set(c.ghostAt.height, y - 0.008 * i);
}
log("deck", deck, "station heights free:", JSON.stringify([...free]));
const under = Math.max(...[...free].filter(([h, ok]) => ok && h < deck).map(([h]) => h));
expect(free.get(deck - 3) === false, "the station refused with its top three units under the path, inside its riders' room");
expect(deck - under >= 4, `the highest station under the path four units or more under it, not ${deck - under}`);
expect(free.get(deck + 3) === true, "the station free three units over the path, as before");
// Laid as high under the path as it may stand, for a look at it.
pointer.move(x, lifts.get(under)!);
await frames(5);
await pointer.click(x, lifts.get(under)!);
pointer.shift(false);
await frames(20);
build.stop();
await frames(5);

// A custom track diving into the ground: its station on the ground, then down medium slopes and level again.
const dive = { col: col + 2, row: row + 6 };
camera.look({ col: dive.col + 0.5, row: dive.row - 3 }, { col: dive.col + 8, row: dive.row + 4, up: 10 });
await frames(5);
const ride = await custom();
pointer.move(...on_view(dive.col + 0.5, dive.row + 0.5));
await frames(10);
await pointer.click(...on_view(dive.col + 0.5, dive.row + 0.5));
await frames(20);
expect(rides.list()[ride].pieces === 1, "the station laid");
async function piece(slope: string, count: number): Promise<void> {
    await ui.click(`placing/body/construction/${slope}`);
    await frames(3);
    for (let i = 0; i < count; ++i) {
        await ui.click("placing/body/construction/AttractionEditor_IconCurve0");
        await frames(10);
    }
}
await piece("AttractionEditor_IconSlope0", 1);
await piece("AttractionEditor_IconSlopeD1", 3);
await piece("AttractionEditor_IconSlope0", 4);
build.stop();
await ui.click("placing/close");
await frames(20);
const names = rides.list()[ride].track ?? [];
log("dive pieces", rides.list()[ride].pieces, JSON.stringify(names));
const track = map.objects().filter((o) => o.overlay.includes("Track") && o.height !== null && o.row === dive.row);
log("track objects on the dive's row", JSON.stringify(track.map((o) => [o.symbol, o.col, o.height, map.tile(o.col, o.row)!.corners])));
// A straight's levels reach 3 over its base; the riders' room 0.75 more and a unit of ground over that make a tunnel.
const straights = track.filter((o) => /^straight$/i.test(o.symbol) && o.height! < 0);
expect(straights.length > 0, "level straights under the ground");
for (const s of straights) {
    const cut = Math.min(...map.tile(s.col, s.row)!.corners) < -0.01;
    expect(cut === s.height! + 3 + 0.75 + 1 > 0, `the ground over the straight at ${s.height} ${cut ? "cut" : "left as a tunnel"} the other way`);
}
// The tunnel's mouth, seen from the trench in front of it.
const mouth = Math.min(...straights.map((s) => s.col));
camera.look({ col: mouth + 1, row: dive.row + 0.5, up: -4.5 }, { col: mouth - 2.5, row: dive.row + 0.5, up: -1 });
await frames(10);
await screenshot("tunnel_mouth");
camera.look({ col: over!.col + 0.5, row: row + 0.5, up: deck - 2 }, { col: over!.col - 2.5, row: row + 5, up: deck + 2 });
await frames(10);
await screenshot("path_over_tile");
