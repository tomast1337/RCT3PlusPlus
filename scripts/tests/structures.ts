// SPDX-License-Identifier: GPL-3.0-or-later
//
// The scenery menu's structure tools on the game's blank landscape. A tree and a wall are put down; a drag over
// their tiles marks them, and the panel saves what stands there as a structure under the name offered. The
// list of structures shows it; picked, it follows the pointer, turns a quarter with the panel's button, and a
// click builds both items again, and a park with money pays what they cost together. The tool stays out for another.
//
//   RCT3PP_DATA=<an empty folder> ./nob script scripts/tests/structures.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

/** How many shown objects there are of `symbol`. */
function count(symbol: string): number {
    return map.objects().filter((o) => o.symbol === symbol).length;
}

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "the build lists");
const gate = map.paths().reduce((a, b) => (b.row > a.row ? b : a));
const col = gate.col + 3;
const row = gate.row - 6;
camera.look({ col: col + 1.5, row: row + 0.5 }, { col: col + 1.5, row: row + 12.5, up: 25 });
await frames(3);

/** Picks `symbol` and puts one down at point `c`, `r` of the map. */
async function place(symbol: string, c: number, r: number): Promise<void> {
    expect(build.pick("Style/Vanilla/Style", symbol), `${symbol} in the scenery lists`);
    await frames(2);
    const at = map.screen(c, r)!;
    pointer.move(at.x, at.y);
    await frames(3);
    expect(build.placing()?.problem === "none", `${symbol} placeable at ${c},${r}, not ${build.placing()?.problem}`);
    await pointer.click(at.x, at.y);
    build.stop();
    await frames(2);
}
const symbols = ["Common_Linden_2", "STD4BrickWall3h"];
await place(symbols[0], col + 0.5, row + 0.5);
await place(symbols[1], col + 1.1, row + 0.5);
const before = symbols.map(count);

// Picking out: the scenery menu's structures item opens its smaller menu; a drag marks the two tiles.
await ui.click("tools/Scenery/Obj_Scenery");
await ui.click("tools/Scenery/SavedSelection_SubMenu");
await ui.click("tools/Scenery/SavedSelection_Grab");
await frames(2);
expect(ui.find("structure_grab") !== undefined, "the picking out panel");
const from = map.screen(col + 0.5, row + 0.5)!;
const to = map.screen(col + 1.5, row + 0.5)!;
await pointer.drag([from.x, from.y], [to.x, to.y], 6);
await frames(3);
expect(structures.marked() === 2, `two tiles marked, not ${structures.marked()}`);
await screenshot("structure_marks");
await ui.click("structure_grab/SavedSelectionGrabber_SaveIcon");
await until(() => ui.find("structure_files") !== undefined, 120, "the save window");
await ui.click("structure_files/content/open");
await until(() => ui.find("structure_files") === undefined, 120, "the save window to close");

// Placing: the list shows the structure saved; picked, it follows the pointer.
await ui.click("tools/Scenery/SavedSelection_List");
const first = "structure_files/content/list/view/content/row0";
await until(() => ui.find(first) !== undefined, 120, "the structure in the list");
await frames(3);
await screenshot("structure_list");
await ui.click(first);
await ui.click("structure_files/content/open");
await until(() => structures.placing() !== undefined, 120, "the structure to place");
const site = { col: col + 1, row: row + 4 };
const at = map.screen(site.col + 0.5, site.row + 0.5)!;
pointer.move(at.x, at.y);
await frames(3);
await ui.click("structure_place/IconAttractionEditorRotate");
pointer.move(at.x, at.y);
await frames(3);
const placing = structures.placing()!;
log("placing", JSON.stringify(placing));
expect(placing.items === 2 && placing.turns === 1, `both items, turned once (${placing.items} items, ${placing.turns} turns)`);
expect(placing.problem === "none" && placing.affordable, `room for it, not ${placing.problem}`);
expect(placing.cost > 0, "a price for the two items");
const cash = park.cash();
await pointer.click(at.x, at.y);
await frames(3);
for (let i = 0; i < symbols.length; ++i)
    expect(count(symbols[i]) === before[i] + 1, `one more ${symbols[i]}, not ${count(symbols[i]) - before[i]}`);
// The landscape is a park without money; one with money pays the cost.
if (cash !== undefined)
    expect(park.cash() === cash - placing.cost, `${placing.cost} paid, not ${cash - (park.cash() ?? 0)}`);
// Turned a quarter, the two items stand one above the other along the rows, on the tile pointed at and next to it.
const built = map.objects().filter((o) => symbols.includes(o.symbol) && Math.abs(o.col - site.col) <= 2 && Math.abs(o.row - site.row) <= 2);
expect(built.length === 2 && built[0].col === built[1].col && Math.abs(built[0].row - built[1].row) === 1, `the two turned along the rows: ${JSON.stringify(built)}`);
expect(structures.placing() !== undefined, "the tool still out for another");
pointer.move(at.x + 40, at.y);
await frames(3);
await screenshot("structure_turned");
quit(0);
