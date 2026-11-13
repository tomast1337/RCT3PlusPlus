// SPDX-License-Identifier: GPL-3.0-or-later
//
// Themed stalls from the older theme packs sell what their kind offers: on the game's blank landscape the
// pumpkin pie and blood milkshake stalls, and the coconut stall whose drink is named after neither food nor
// drink, are placed beside the road in from the entrance. Each must come with its kind's goods on its menu,
// open, and sell to guests dropped on the paths.
//
//   ./nob script scripts/tests/stall_goods.ts -- --no-audio

export {};

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.stalls() !== undefined, 1200, "the build lists");
const paths = map.paths();
const gate = paths.reduce((a, b) => (b.row > a.row ? b : a));
const wanted = ["pumpkinpiestall", "BloodMilkStall", "CocnutStall"];
const at: number[] = [];
for (let k = 0; k < wanted.length; ++k) {
    const kind = build.stalls()!.find((s) => s.symbol.toLowerCase() === wanted[k].toLowerCase());
    expect(kind !== undefined, `the ${wanted[k]} to pick`);
    log(kind!.symbol, kind!.overlay, kind!.goods.map((g) => `${g.item} ${g.price}`).join(", "));
    expect(kind!.goods.length > 0, `${kind!.symbol} selling something`);
    const road = paths.find((p) => p.col === gate.col && p.row === gate.row - 4 - 2 * k);
    expect(road !== undefined, `the road in from the entrance at row ${gate.row - 4 - 2 * k}`);
    camera.look({ col: gate.col + 0.5, row: road!.row + 0.5 }, { col: gate.col + 0.5, row: road!.row + 11.5, up: 25 });
    await frames(3);
    expect(build.pick(kind!.overlay, kind!.symbol), `${kind!.symbol} picked`);
    await frames(2);
    // Turned a quarter from where it starts it faces west, to the road; the tool keeps the last stall's turn.
    const spot = must(map.screen(road!.col + 1, road!.row), "the tile in sight");
    pointer.move(spot.x, spot.y);
    await frames(3);
    for (let turn = 0; turn < 4 && build.placing()?.site?.turns !== 1; ++turn) {
        await ui.click("placing/IconAttractionEditorRotate");
        pointer.move(spot.x, spot.y);
        await frames(3);
    }
    expect(build.placing()?.site?.turns === 1, `${kind!.symbol} turned to the road (${JSON.stringify(build.placing())})`);
    expect(build.placing()?.problem === "none", `${kind!.symbol} placeable beside the road, not ${build.placing()?.problem}`);
    await pointer.click(spot.x, spot.y);
    await frames(3);
    if (ui.find("info") !== undefined)
        await ui.click("info/close");
    const i = stalls.list().findIndex((s) => s.symbol === `${kind!.symbol}:sta`);
    expect(i >= 0, `${kind!.symbol} in the park`);
    const stall = stalls.list()[i];
    expect(JSON.stringify(stall.items.map((x) => x.item)) === JSON.stringify(kind!.goods.map((g) => `${g.item}:cid`)), `${kind!.symbol} selling its kind's goods (${JSON.stringify(stall.items)})`);
    const why = stalls.setState(i, "open");
    expect(why == null, `${kind!.symbol} opened, not kept closed by ${why}`);
    at.push(i);
}
await frames(2);
for (const i of at)
    expect(stalls.list()[i].selling === true, `${stalls.list()[i].symbol} run by the park, open (${JSON.stringify(stalls.list()[i])})`);
park.dropParties(60, 3);
const served = () => at.map((i) => `${stalls.list()[i].symbol} ${stalls.list()[i].customers}`).join(", ");
for (let t = 0; t < 30 && !at.every((i) => (stalls.list()[i].customers ?? 0) > 0); ++t)
    await park.advance(120);
log("customers", served(), "guests", park.guests());
expect(at.every((i) => (stalls.list()[i].customers ?? 0) > 0), `guests buying at every stall (${served()})`);
park.setTime(0.5);
camera.look({ col: gate.col + 1.5, row: gate.row - 6.5 }, { col: gate.col - 4.5, row: gate.row - 1.5, up: 7 });
await frames(240);
await screenshot("stall_goods");
quit(0);
