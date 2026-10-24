// SPDX-License-Identifier: GPL-3.0-or-later
//
// Places a stall as a player would, on the game's blank landscape: picked, turned once with the placing panel's
// tab to face the road in from the park's entrance, refused on the road itself, then placed on the grass beside
// it. The tool must go away, the stall's window open, and the new stall sell everything its kind offers, closed.
//
//   ./nob script scripts/tests/stall_placing.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.stalls() !== undefined, 1200, "the build lists");
const plain = build.stalls()!.filter((s) => s.overlay === "Style/Vanilla/Style" && s.goods.length > 0);
log("plain stalls", plain.map((s) => `${s.symbol} ${s.goods.map((g) => `${g.item} ${g.price}`).join(", ")}`).join("; "));
const kind = plain.find((s) => s.symbol.toLowerCase().includes("burger")) ?? plain[0];
expect(kind !== undefined, "a plain stall that sells something");
const paths = map.paths();
const gate = paths.reduce((a, b) => (b.row > a.row ? b : a));
const road = paths.find((p) => p.col === gate.col && p.row === gate.row - 4);
expect(road !== undefined, "the road in from the entrance");
camera.look({ col: gate.col + 0.5, row: gate.row - 4.5 }, { col: gate.col + 0.5, row: gate.row + 6.5, up: 25 });
await frames(3);

/** What placing shows with the pointer resting on tile `col`, `row`. */
async function hover(col: number, row: number): Promise<{ x: number; y: number; problem: string }> {
    const at = map.screen(col, row);
    expect(at !== undefined, `tile ${col},${row} in sight`);
    pointer.move(at!.x, at!.y);
    await frames(3);
    return { ...at!, problem: build.placing()?.problem ?? "nothing placing" };
}

expect(build.pick(kind.overlay, kind.symbol), `${kind.symbol} picked`);
await frames(2);
// A quarter turn from where it starts faces it the way of falling columns, to the road west of it.
await ui.click("placing/IconAttractionEditorRotate");
const onRoad = await hover(road!.col, road!.row);
expect(onRoad.problem === "path", `the stall refused on the road, not ${onRoad.problem}`);
const grass = await hover(road!.col + 1, road!.row);
expect(grass.problem === "none", `the stall placeable beside the road, not ${grass.problem}`);
await pointer.click(grass.x, grass.y);
await frames(3);
expect(build.placing() === undefined, "the tool put away once the stall stands");
const placed = stalls.list().filter((s) => s.symbol === `${kind.symbol}:sta`);
expect(placed.length === 1, `one ${kind.symbol} in the park, not ${placed.length}`);
const stall = placed[0];
log("placed", JSON.stringify(stall));
expect(stall.site?.col === road!.col + 1 && stall.site?.row === road!.row && stall.site?.direction === 1, `it on the tile beside the road, facing it (${JSON.stringify(stall.site)})`);
expect(stall.name === "%ME% 1", `it named after its kind, not ${stall.name}`);
expect(!stall.open, "it built closed");
expect(JSON.stringify(stall.items) === JSON.stringify(kind.goods.map((g) => ({ item: `${g.item}:cid`, price: g.price }))), "it selling all its kind's goods at their prices");
expect(stall.shown && ui.find("info") !== undefined, "its window open");
// The park's objects are built again a few times a second, beside the frame; a frame here takes far less.
camera.look({ col: road!.col + 1.5, row: road!.row + 0.5 }, { col: road!.col + 1.5, row: road!.row + 4.5, up: 8 });
await frames(300);
await screenshot("stall_placing");
quit(0);
