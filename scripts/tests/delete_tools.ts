// SPDX-License-Identifier: GPL-3.0-or-later
//
// The delete menu's tools on the game's blank landscape. With the single item tool out, a click on a stall asks
// before pulling it down, a bench on the road and a bare piece of road go at once with no question, road under a
// bin takes the bin along, and the tool stays out until it is picked again. With the area tool, a drag marks
// tiles; Clear forgets them, and Delete takes the road and the bench there at once and asks about the stall
// standing there. A bench's own panel pulls it down as well.
//
//   ./nob script scripts/tests/delete_tools.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.stalls() !== undefined, 1200, "the build lists");
const paths = map.paths();
const gate = paths.reduce((a, b) => (b.row > a.row ? b : a));
// The road in from the entrance, clear of the entrance itself: a bench, a bare tile, and a stall beside.
const road = paths.filter((p) => p.col === gate.col && p.row < gate.row - 1).sort((a, b) => b.row - a.row);
const bench = road[0];
const bare = road[2];
const binned = road[4];
camera.look({ col: gate.col + 0.5, row: gate.row - 4.5 }, { col: gate.col + 0.5, row: gate.row + 8.5, up: 30 });
await frames(3);

/** Clicks tile `col`, `row`, moved `up` and `right` by those parts of the screen, and lets a few frames go by. */
async function click(col: number, row: number, up = 0, right = 0): Promise<void> {
    const at = map.screen(col, row);
    expect(at !== undefined, `tile ${col},${row} in sight`);
    await pointer.click(at!.x + right, at!.y - up);
    await frames(3);
}

expect(build.pick("Style/Vanilla/Style", "Bench"), "a bench in the scenery lists");
await frames(2);
await click(bench.col, bench.row);
build.stop();
expect(build.pick("Style/Vanilla/Style", "Bin"), "a bin in the scenery lists");
await frames(2);
await click(binned.col, binned.row);
build.stop();
const items = park.pathItems().length;
expect(items === 2, `the bench and the bin on the road, not ${items}`);
const kind = build.stalls()!.find((s) => s.overlay === "Style/Vanilla/Style" && s.goods.length > 0)!;
expect(build.pick(kind.overlay, kind.symbol), `${kind.symbol} picked`);
await frames(2);
const beside = { col: bare.col - 1, row: bare.row - 2 };
pointer.move(map.screen(beside.col, beside.row)!.x, map.screen(beside.col, beside.row)!.y);
await frames(3);
expect(build.placing()?.problem === "none", `the stall placeable beside the road, not ${build.placing()?.problem}`);
await click(beside.col, beside.row);
const stall = stalls.list().findIndex((s) => s.symbol === `${kind.symbol}:sta`);
expect(stall >= 0, "the stall placed");
await ui.click("info/close").catch(() => undefined);
await frames(2);

if (!ui.find("tools/Delete/GUI2_DeleteSingleItem"))
    await ui.click("tools/Delete/Obj_Delete");
await ui.click("tools/Delete/GUI2_DeleteSingleItem");
await frames(2);

// The stall: asked about, No keeps it, Yes pulls it down.
await click(beside.col, beside.row, 0.02);
expect(ui.find("demolish_question") !== undefined, "the question asked for the stall");
await screenshot("delete_tool_question");
await ui.click("demolish_question/content/GUI2MiniDialog_Cancel");
await frames(2);
expect(!stalls.list()[stall].demolished, "No keeping the stall");
await click(beside.col, beside.row, 0.02);
await ui.click("demolish_question/content/GUI2MiniDialog_Confirm");
await frames(2);
expect(stalls.list()[stall].demolished, "the stall pulled down");

// The bench and the bare road: no question.
// A bench stands at the side of its tile.
await click(bench.col, bench.row, 0.01, 0.016);
expect(ui.find("demolish_question") === undefined, "no question for a bench");
expect(park.pathItems().length === items - 1, `the bench gone (${items} then ${park.pathItems().length})`);
await screenshot("delete_bench"); // What it cost to pull down floats up, even when nothing
const pieces = map.paths().length;
const cash = park.cash() ?? 0;
await click(bare.col, bare.row);
expect(map.paths().length === pieces - 1, `the bare road gone (${pieces} then ${map.paths().length})`);
log("road sold for", (park.cash() ?? 0) - cash);
expect(!map.paths().some((p) => p.col === bare.col && p.row === bare.row), "no road left on its tile");
// The road under the bin takes the bin with it.
await click(binned.col, binned.row, 0, -0.01);
expect(map.paths().length === pieces - 2, "the road under the bin gone");
expect(park.pathItems().length === items - 2, `the bin gone with it, not left standing (${park.pathItems().length} left)`);
await screenshot("delete_binned_road"); // One sum floats up for the road and its bin together

// Picking the tool again puts it away; a click on the road then leaves it be.
if (!ui.find("tools/Delete/GUI2_DeleteSingleItem"))
    await ui.click("tools/Delete/Obj_Delete");
await ui.click("tools/Delete/GUI2_DeleteSingleItem");
await frames(2);
await click(road[3].col, road[3].row);
expect(map.paths().length === pieces - 2, "the road kept once the tool is put away");
await screenshot("delete_tool");

// A bench's own panel pulls it down too, paid for as the tool does.
expect(build.pick("Style/Vanilla/Style", "Bench"), "a bench in the scenery lists");
await frames(2);
await click(road[5].col, road[5].row);
build.stop();
const placed = park.pathItems().length;
await click(road[5].col, road[5].row, 0.01, 0.016);
expect(ui.find("info") !== undefined, "the bench's panel open");
await ui.click("info/Attraction_General_Delete");
await frames(3);
expect(park.pathItems().length === placed - 1, `the bench pulled down from its panel (${placed} then ${park.pathItems().length})`);
await screenshot("destroy_bench");

// The area tool: a stall beside the road and a bench on it, then the road there and both marked.
expect(build.pick("Style/Vanilla/Style", "Bench"), "a bench in the scenery lists");
await frames(2);
await click(road[7].col, road[7].row);
build.stop();
expect(build.pick(kind.overlay, kind.symbol), `${kind.symbol} picked again`);
await frames(2);
const aside = { col: road[7].col - 1, row: road[7].row - 2 };
pointer.move(map.screen(aside.col, aside.row)!.x, map.screen(aside.col, aside.row)!.y);
await frames(3);
expect(build.placing()?.problem === "none", `the second stall placeable beside the road, not ${build.placing()?.problem}`);
await click(aside.col, aside.row);
const second = stalls.list().length - 1;
expect(second > stall, "the second stall placed");
await ui.click("info/close").catch(() => undefined);
await frames(2);
const before = { paths: map.paths().length, items: park.pathItems().length };

if (!ui.find("tools/Delete/GUI2_DeleteSelection"))
    await ui.click("tools/Delete/Obj_Delete");
await ui.click("tools/Delete/GUI2_DeleteSelection");
await frames(2);
expect(ui.find("delete_area") !== undefined, "the area panel out");
// From the road below the bench to past the stall.
const site = stalls.list()[second].site!;
const corner = { col: Math.min(site.col, gate.col) - 1, row: Math.min(site.row, road[7].row) - 1 };
const from = map.screen(road[6].col + 1, road[6].row)!;
const to = map.screen(corner.col, corner.row)!;
const marked = map.paths().filter((p) => p.col >= corner.col && p.col <= road[6].col + 1 && p.row >= corner.row && p.row <= road[6].row).length;
await pointer.drag([from.x, from.y], [to.x, to.y], 10);
await frames(3);
await screenshot("delete_area_marked");
// Clear forgets the marks and deletes nothing.
await ui.click("delete_area/GUI2_DeleteSelection_ClearIcon");
await frames(2);
expect(map.paths().length === before.paths && park.pathItems().length === before.items, "nothing gone on Clear");
await pointer.drag([from.x, from.y], [to.x, to.y], 10);
await frames(3);
await ui.click("delete_area/GUI2_DeleteSelection_DeleteIcon");
await frames(2);
expect(map.paths().length === before.paths - marked, `the ${marked} tiles of road marked gone (${before.paths} then ${map.paths().length})`);
expect(park.pathItems().length === before.items - 1, "the bench gone");
expect(ui.find("demolish_question") !== undefined, "the question asked for the stall in the area");
await screenshot("delete_area_question");
await ui.click("demolish_question/content/GUI2MiniDialog_Confirm");
await frames(3);
expect(stalls.list()[second].demolished, "the second stall pulled down");
await screenshot("delete_area");
quit(0);
