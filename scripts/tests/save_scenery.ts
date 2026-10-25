// SPDX-License-Identifier: GPL-3.0-or-later
//
// Places a tree, a wall and a bench on the game's blank landscape, saves the game and loads it again: all three
// are still there, and what the park makes itself on loading (supports, fences, trees beyond the edge) is made
// once, not once more on top of what was saved.
//
//   RCT3PP_DATA=<an empty folder> ./nob script scripts/tests/save_scenery.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

/** Clicks a tool's own button, which comes first in its group. */
async function click_tool(tool: string): Promise<void> {
    await ui.click(`tools/${tool}/${ui.children(`tools/${tool}`)![0]}`);
}

/** How many shown objects there are of each symbol. */
function counts(): Map<string, number> {
    const out = new Map<string, number>();
    for (const o of map.objects())
        out.set(o.symbol, (out.get(o.symbol) ?? 0) + 1);
    return out;
}

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "the build lists");
const gate = map.paths().reduce((a, b) => (b.row > a.row ? b : a));
const col = gate.col + 2;
const row = gate.row - 4;
camera.look({ col: col + 0.5, row: row + 0.5 }, { col: col + 0.5, row: row + 9.5, up: 20 });
await frames(3);

/** Picks `symbol` and puts one down at point `c`, `r` of the map. */
async function place(symbol: string, c: number, r: number): Promise<void> {
    expect(build.pick("Style/Vanilla/Style", symbol), `${symbol} in the scenery lists`);
    await frames(2);
    const at = map.screen(c, r);
    expect(at !== undefined, `point ${c},${r} in sight`);
    pointer.move(at!.x, at!.y);
    await frames(3);
    const problem = build.placing()?.problem;
    expect(problem === "none", `${symbol} placeable at ${c},${r}, not ${problem}`);
    await pointer.click(at!.x, at!.y);
    build.stop();
    await frames(2);
}
const before = counts();
await place("Common_Linden_2", col + 0.5, row - 2.5);
await place("STD4BrickWall3h", col + 0.1, row + 0.5);
await place("Bench", gate.col, gate.row - 3);
const placed = counts();
const symbols = ["Common_Linden_2", "STD4BrickWall3h", "Bench"];
for (const symbol of symbols)
    expect((placed.get(symbol) ?? 0) > (before.get(symbol) ?? 0), `more ${symbol} once placed`);
const benches = park.pathItems().filter((i) => i.kind === "bench").length;
await screenshot("placed");

await click_tool("Files");
await ui.click("tools/Files/File_SaveGame");
await until(() => ui.find("park_files") !== undefined, 120, "the save window");
await ui.click("park_files/content/open");
await until(() => ui.find("park_files") === undefined, 120, "the save window to close");

camera.look({ col: 2, row: 2 }, { col: 2, row: -10, up: 20 });
await frames(3);
expect(map.screen(col + 0.5, row + 0.5) === undefined, "the scenery out of sight");
if (!ui.find("tools/Files/File_LoadGame"))
    await click_tool("Files");
await until(() => ui.find("tools/Files/File_LoadGame") !== undefined, 120, "the files menu");
await ui.click("tools/Files/File_LoadGame");
const first = "park_files/content/list/view/content/row0";
await until(() => ui.find(first) !== undefined, 120, "the save in the load window");
await ui.click(first);
await ui.click("park_files/content/open");
// The view looks away; the saved park's own looks back at the scenery once it has loaded.
await until(() => map.screen(col + 0.5, row + 0.5) !== undefined, 1200, "the saved park, looking at the scenery");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "the build lists");
await frames(10);

// As many of each as before saving: the player's scenery kept, and what the park makes itself made only once.
const loaded = counts();
for (const symbol of new Set([...placed.keys(), ...loaded.keys()]))
    expect(loaded.get(symbol) === placed.get(symbol), `${placed.get(symbol)} ${symbol} after loading, not ${loaded.get(symbol)}`);
log("objects", map.objects().length, "of which supports", map.objects().filter((o) => o.overlay.startsWith("Supports/")).length);
expect(park.pathItems().filter((i) => i.kind === "bench").length === benches, "the bench used by guests again");
camera.look({ col: col + 0.5, row: row + 0.5 }, { col: col + 0.5, row: row + 9.5, up: 20 });
await frames(10);
await screenshot("loaded");
quit(0);
