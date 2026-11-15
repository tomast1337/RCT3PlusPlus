// SPDX-License-Identifier: GPL-3.0-or-later
//
// Park maps, on the game's blank landscape: an information kiosk beside the road in from the entrance, which
// sells umbrellas and maps. Guests dropped on the paths in dry weather want no umbrella, but parties without a
// map now and then go to the kiosk and buy one.
//
//   ./nob script scripts/tests/park_maps.ts -- --no-audio

export {};

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => build.stalls() !== undefined, 1200, "the build lists");
const gate = map.paths().reduce((a, b) => (b.row > a.row ? b : a));
const road = must(
    map.paths().find((p) => p.col === gate.col && p.row === gate.row - 4),
    "the road in from the entrance",
);

// The kiosk east of the road, turned to face it.
const kiosk = must(
    build.stalls()!.find((s) => s.goods.some((g) => g.item.toLowerCase().includes("map"))),
    "a stall that sells maps",
);
log("kiosk", kiosk.symbol, kiosk.goods.map((g) => g.item).join(", "));
camera.look({ col: road.col + 0.5, row: road.row + 0.5 }, { col: road.col + 0.5, row: road.row + 10.5, up: 25 });
await frames(3);
expect(build.pick(kiosk.overlay, kiosk.symbol), `${kiosk.symbol} picked`);
const spot = must(map.screen(road.col + 1, road.row), "the kiosk's tile in sight");
pointer.move(spot.x, spot.y);
await frames(3);
for (let turn = 0; turn < 4 && build.placing()?.site?.turns !== 1; ++turn) {
    await ui.click("placing/IconAttractionEditorRotate");
    pointer.move(spot.x, spot.y);
    await frames(3);
}
expect(build.placing()?.problem === "none", `the kiosk placeable, not ${JSON.stringify(build.placing())}`);
await pointer.click(spot.x, spot.y);
await frames(3);
build.stop();
if (ui.find("info") !== undefined) await ui.click("info/close");
const stall = stalls.list().findIndex((s) => s.symbol === `${kiosk.symbol}:sta`);
expect(stall >= 0, "the kiosk in the park");
expect(stalls.setState(stall, "open") == null, "the kiosk open");

park.setTime(0.4);
park.dropParties(60, 3);
// Dry all along, so what the kiosk sells is maps, not umbrellas.
for (let ran = 0; (stalls.list()[stall].customers ?? 0) === 0; ran += 5) {
    expect(ran < 1800, "a map sold within 1800 park seconds");
    park.setWeather("sunny");
    await park.advance(5);
}
log("maps sold", stalls.list()[stall].customers);
park.setTime(0.5);
camera.look({ col: road.col + 1.5, row: road.row + 0.5, up: 0.5 }, { col: road.col - 2.5, row: road.row + 3.5, up: 5 });
await frames(10);
await screenshot("park_maps_kiosk");
quit(0);
