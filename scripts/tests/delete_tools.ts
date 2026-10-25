// SPDX-License-Identifier: GPL-3.0-or-later
//
// The delete menu's tools on the game's blank landscape. With the single item tool out, a click on a stall asks
// before pulling it down, a bench on the road and a bare piece of road go at once with no question, road under a bin takes the bin along, and the tool stays out until it is picked again.
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

// Picking the tool again puts it away; a click on the road then leaves it be.
if (!ui.find("tools/Delete/GUI2_DeleteSingleItem"))
    await ui.click("tools/Delete/Obj_Delete");
await ui.click("tools/Delete/GUI2_DeleteSingleItem");
await frames(2);
await click(road[3].col, road[3].row);
expect(map.paths().length === pieces - 2, "the road kept once the tool is put away");
await screenshot("delete_tool");
quit(0);
