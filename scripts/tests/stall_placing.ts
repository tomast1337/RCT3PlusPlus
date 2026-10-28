// SPDX-License-Identifier: GPL-3.0-or-later
//
// Places a stall as a player would, on the game's blank landscape: picked, turned once with the placing panel's
// tab to face the road in from the park's entrance, refused on the road itself (a click there builds nothing and is
// silent), then placed on the grass beside it, with a thump. Its panel's levelling tabs light one at a time, none at first. The tool must go away, the stall's window open, and the new stall sell everything its kind offers, closed;
// the park must run it, as it faces the road. Its window then takes items off its menu and puts them back.
// A drink stall, whose model has a motion, then plays it as soon as it stands, closed.
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
expect(build.placing()?.flatten === "off", "the ground left as it is at first");
for (const [tab, after] of [["SharpOff", "sharp"], ["SmoothOff", "smooth"], ["SmoothOn", "off"]]) {
    await ui.click(`placing/TerrainFlattener_${tab}`);
    await frames(2);
    expect(build.placing()?.flatten === after, `the ground levelled ${after} after ${tab}, not ${build.placing()?.flatten}`);
}
// A quarter turn from where it starts faces it the way of falling columns, to the road west of it.
await ui.click("placing/IconAttractionEditorRotate");
const onRoad = await hover(road!.col, road!.row);
expect(onRoad.problem === "path", `the stall refused on the road, not ${onRoad.problem}`);
const thumps = build.sounds().thumps;
await pointer.click(onRoad.x, onRoad.y);
await frames(2);
expect(build.sounds().thumps === thumps && build.placing() !== undefined, "a click on the road building nothing, silent");
const grass = await hover(road!.col + 1, road!.row);
expect(grass.problem === "none", `the stall placeable beside the road, not ${grass.problem}`);
expect(build.placing()?.arrow === true, "an arrow on the side the stall serves from");
await screenshot("stall_arrow");
await pointer.click(grass.x, grass.y);
await frames(3);
expect(build.placing() === undefined, "the tool put away once the stall stands");
expect(build.sounds().thumps === thumps + 1, `a thump as the stall went down (${build.sounds().thumps - thumps})`);
const placed = stalls.list().filter((s) => s.symbol === `${kind.symbol}:sta`);
expect(placed.length === 1, `one ${kind.symbol} in the park, not ${placed.length}`);
const stall = placed[0];
log("placed", JSON.stringify(stall));
expect(stall.site?.col === road!.col + 1 && stall.site?.row === road!.row && stall.site?.direction === 1, `it on the tile beside the road, facing it (${JSON.stringify(stall.site)})`);
expect(stall.name === "%ME% 1", `it named after its kind, not ${stall.name}`);
expect(!stall.open, "it built closed");
expect(!stall.moving, "a stall whose model has no motion standing still");
expect(JSON.stringify(stall.items.map((i) => ({ item: i.item, price: i.price }))) === JSON.stringify(kind.goods.map((g) => ({ item: `${g.item}:cid`, price: g.price }))), "it selling all its kind's goods at their prices");
expect(stall.shown && ui.find("info") !== undefined, "its window open");
// The road runs past the tile it faces, so the park runs it at once, closed.
await frames(2);
const running = stalls.list().find((s) => s.symbol === `${kind.symbol}:sta`)!;
expect(running.joined && running.selling === false && running.price === kind.goods[0].price, `the park running it, closed, at its first item's price (${JSON.stringify(running)})`);

// Its window's items page takes items off the menu and puts them back. Bare, it cannot open.
const at = stalls.list().findIndex((s) => s.symbol === `${kind.symbol}:sta`);
const menu = () => stalls.list()[at];
const opened = stalls.setState(at, "open");
expect(opened == null && menu().selling === true, `it opened (${opened}, ${JSON.stringify(menu())})`);
await ui.click("info/Stall_ItemsIcon");
await frames(2);
await ui.click("info/body/StallMenu/Stall_Synchronize");
await frames(2);
expect(menu().sync, "its synchronise tick on");
// The menu: goods stacked up from the bin at its bottom-left, a row for each item, its handle left of its name.
// Places are fractions of the screen; the synchronise tick is one cell of the strip across and down.
const pane = ui.find("info/body/StallMenu")!;
const tick = ui.find("info/body/StallMenu/Stall_Synchronize")!;
const across = tick.right - tick.left;
const down = tick.bottom - tick.top;
const bin: [number, number] = [pane.left + across / 2, pane.bottom - down / 2];
const good = (i: number): [number, number] => [bin[0], bin[1] - down * (i + 1)];
const handle = (i: number): [number, number] => {
    const name = ui.find(`info/body/StallMenu/menu/view/content/ItemName${i}`)!;
    return [name.left - across / 2, name.y];
};
await frames(2);
await screenshot("stall_menu");
if (kind.goods.length > 1) {
    // Dragged by its handle below the next item, the first item moves down one.
    const second = handle(1);
    await pointer.drag(handle(0), [second[0] + across * 3, second[1] + down * 0.4], 8);
    await frames(2);
    expect(menu().items[1].item === `${kind.goods[0].item}:cid`, `the first item moved down one (${JSON.stringify(menu().items)})`);
}
// Dragged onto the bin, an item goes off; a click on a handle and then on the bin takes one off too.
await pointer.drag(handle(0), bin, 8);
await frames(2);
for (let i = 1; i < kind.goods.length; ++i) {
    const [x, y] = handle(0);
    await pointer.click(x, y);
    await frames(1);
    await pointer.click(bin[0], bin[1]);
    await frames(2);
}
expect(menu().items.length === 0 && !menu().open, `it emptied and closed (${JSON.stringify(menu())})`);
expect(stalls.setState(at, "open") === "CantOpenStallNoItems", "a stall with nothing to sell refused opening");
// A click on a good puts it on; one dragged onto the pane goes in where it drops.
await pointer.click(...good(kind.goods.length - 1));
await frames(2);
expect(menu().items.length === 1 && menu().items[0].item === `${kind.goods[kind.goods.length - 1].item}:cid`, `its last good back on (${JSON.stringify(menu().items)})`);
const first = handle(0);
await pointer.drag(good(0), [first[0] + across * 4, first[1] - down * 0.4], 8);
await frames(2);
const back = menu();
expect(back.items.length === 2 && back.items[0].item === `${kind.goods[0].item}:cid` && back.items[0].price === kind.goods[0].price, `its first good dragged on before it (${JSON.stringify(back)})`);
expect(back.price === kind.goods[0].price, "the park selling it at that item's price");
expect(stalls.setState(at, "open") == null && menu().selling === true, "it opened again");
// The park's objects are built again a few times a second, beside the frame; a frame here takes far less.
camera.look({ col: road!.col + 1.5, row: road!.row + 0.5 }, { col: road!.col + 1.5, row: road!.row + 4.5, up: 8 });
await frames(300);
await screenshot("stall_placing");

// A drink stall's model has a motion of its own: it plays at once, round and round, closed as it is built.
const drinks = build.stalls()!.find((s) => s.symbol === "DrinkStall");
expect(drinks !== undefined, "the drink stall to pick");
expect(build.pick(drinks!.overlay, drinks!.symbol), "the drink stall picked");
await frames(2);
camera.look({ col: road!.col + 0.5, row: road!.row - 4.5 }, { col: road!.col + 0.5, row: road!.row + 6.5, up: 25 });
await frames(3);
const spot = await hover(road!.col + 1, road!.row - 2);
expect(spot.problem === "none", `the drink stall placeable up the road, not ${spot.problem}`);
await pointer.click(spot.x, spot.y);
await frames(3);
const drink = stalls.list().find((s) => s.symbol === "DrinkStall:sta");
expect(drink !== undefined && !drink.open && drink.moving, `the drink stall moving, closed (${JSON.stringify(drink)})`);
camera.look({ col: road!.col + 1.5, row: road!.row - 1.5, up: 1.5 }, { col: road!.col - 2, row: road!.row - 1.5, up: 4 });
await frames(30);
await screenshot("drink_stall_a");
await frames(45);
await screenshot("drink_stall_b");
quit(0);
