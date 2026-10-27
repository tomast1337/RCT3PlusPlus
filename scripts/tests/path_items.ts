// SPDX-License-Identifier: GPL-3.0-or-later
//
// Places the things guests use on paths, as a player would, on the game's blank landscape: a bench, a bin, a
// lamp and two signs. Each must be refused on the grass, go on its own tile of the road in from the park's
// entrance, and then be refused where it already stands.
//
//   ./nob script scripts/tests/path_items.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "the build lists");
const paths = map.paths();
const gate = paths.reduce((a, b) => (b.row > a.row ? b : a));
const symbols = ["Bench", "Bin", "LampVer1", "SignSquare", "Signsmall"];
// The road in from the entrance, clear of the entrance itself, a tile for each item.
const road = paths.filter((p) => p.col === gate.col && p.row < gate.row - 1).sort((a, b) => b.row - a.row).slice(0, symbols.length);
expect(road.length === symbols.length, `${symbols.length} tiles of road, not ${road.length}`);
camera.look({ col: gate.col + 0.5, row: gate.row - 4.5 }, { col: gate.col + 0.5, row: gate.row + 8.5, up: 30 });
await frames(3);

/** What placing shows with the pointer resting on tile `col`, `row`. */
async function hover(col: number, row: number): Promise<{ x: number; y: number; problem: string }> {
    const at = map.screen(col, row);
    expect(at !== undefined, `tile ${col},${row} in sight`);
    pointer.move(at!.x, at!.y);
    await frames(3);
    return { ...at!, problem: build.placing()?.problem ?? "nothing placing" };
}

for (const [i, symbol] of symbols.entries()) {
    const tile = road[i];
    expect(build.pick("Style/Vanilla/Style", symbol), `${symbol} in the scenery lists`);
    await frames(2);
    const grass = await hover(tile.col + 3, tile.row);
    const path = await hover(tile.col, tile.row);
    log(symbol, "on grass", grass.problem, "on the path", path.problem);
    expect(grass.problem === "off path", `${symbol} refused on the grass`);
    expect(path.problem === "none", `${symbol} placeable on the path`);
    await pointer.click(path.x, path.y);
    const again = await hover(tile.col, tile.row);
    expect(again.problem === "blocked", `${symbol} refused where it now stands, not ${again.problem}`);
    build.stop();
    await frames(1);
}
// A slope: two pieces climbing east off the road, past a level stretch so neither end of the first is free. A bin
// still goes on it; a bench, kept to level paths, does not.
const climb = paths.filter((p) => p.col === gate.col && p.row < gate.row - 1).sort((a, b) => b.row - a.row)[symbols.length + 2];
expect(climb !== undefined, "road left for the climb");
camera.look({ col: climb.col + 2.5, row: climb.row + 0.5 }, { col: climb.col + 2.5, row: climb.row + 9.5, up: 25 });
await frames(3);
await ui.click("tools/Paths/Obj_Paths");
await until(() => ui.find("footpaths/route") !== undefined, 120, "the path panel");
await ui.toggle("footpaths/follow", false);
await ui.toggle("footpaths/route", false);
const laid = map.paths().length;
const east = [map.screen(climb.col + 1, climb.row)!, map.screen(climb.col + 2, climb.row)!];
await pointer.drag([east[0].x, east[0].y], [east[1].x, east[1].y], 10);
await ui.click("footpaths/body/Path_UpwardSlope");
await ui.click("footpaths/body/Path_Straight");
await ui.click("footpaths/body/Path_Straight");
await ui.click("footpaths/body/Path_NoSlope");
await ui.click("footpaths/body/Path_Straight");
build.stop();
await frames(5);
const sloped = map.paths().slice(laid).filter((p) => p.raised);
log("climb", JSON.stringify(map.paths().slice(laid)));
expect(sloped.length >= 2, `a raised climb, not ${sloped.length} raised pieces`);
for (const [symbol, problem] of [["Bin", "none"], ["Bench", "blocked"]]) {
    expect(build.pick("Style/Vanilla/Style", symbol), `${symbol} in the scenery lists`);
    await frames(2);
    const on = await hover(sloped[0].col, sloped[0].row);
    log(symbol, "on the slope", on.problem);
    expect(on.problem === problem, `${symbol} on the slope ${problem}, not ${on.problem}`);
}
await screenshot("path_items_slope");
build.stop();
await frames(1);

// Through the interface now: the bench's row in the path extras list. A right drag moves the camera and keeps the
// placing and its ghost; a still right click puts it away; its row picks it again, also once the tool row's
// button has been pressed again while its menu is out.
const scenery = `tools/Scenery/${ui.children("tools/Scenery")![0]}`;
const extras = "tools/Scenery/Obj_Scenery_PathExtras";
const bench = "build_list/content/list/view/content/Bench";
async function pickBench(): Promise<void> {
    if (!ui.find(bench)) {
        if (!ui.find(extras))
            await ui.click(scenery);
        await until(() => ui.find(extras) !== undefined, 120, "the scenery menu");
        await ui.click(extras);
        await until(() => ui.find(bench) !== undefined, 120, "the bench in the path extras list");
        await frames(2); // Laid out
    }
    // Rolled into sight in the list, which holds more than it shows.
    const list = ui.find("build_list/content/list")!;
    for (let i = 0; i < 60 && ui.find(bench)!.bottom > list.bottom; ++i) {
        pointer.move(list.x, list.y);
        pointer.wheel(-1);
        await frames(2);
    }
    expect(ui.find(bench)!.bottom <= list.bottom, "the bench's row rolled into sight");
    await ui.click(bench);
    await frames(2);
}
// The list sits in the middle of the screen; the road goes to the left of it.
camera.look({ col: gate.col + 8.5, row: gate.row - 4.5 }, { col: gate.col + 8.5, row: gate.row + 8.5, up: 30 });
await pickBench();
const before = await hover(road[1].col, road[1].row);
expect(build.placing()?.ghost === true, `the bench's ghost on the path, ${before.problem}`);
await pointer.drag([before.x, before.y], [before.x + 0.04, before.y], 10, "right");
expect(build.placing() !== undefined, "the bench still placing after a right drag");
await hover(road[1].col, road[1].row);
expect(build.placing()?.ghost === true, "the bench's ghost after a right drag");
await screenshot("path_items_after_right_drag");
const still = await hover(road[1].col, road[1].row);
await pointer.click(still.x, still.y, "right");
expect(build.placing() === undefined, "a right click to put the placing away");
await pickBench();
await hover(road[1].col, road[1].row);
expect(build.placing()?.ghost === true, "the bench picked again from its row");
await pointer.click(still.x, still.y, "right");
// The tool row's button again while its menu is out folds the menu; the list's row still picks.
if (!ui.find(extras))
    await ui.click(scenery);
await until(() => ui.find(extras) !== undefined, 120, "the scenery menu");
await ui.click(scenery);
log("after the tool button again: menu", ui.find(extras) !== undefined, "list", ui.find(bench) !== undefined);
await pickBench();
await hover(road[1].col, road[1].row);
expect(build.placing()?.ghost === true, "the bench picked again after the tool row's button");
await screenshot("path_items");
quit(0);
