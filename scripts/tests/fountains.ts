// SPDX-License-Identifier: GPL-3.0-or-later
//
// Places a cupid fountain and a dolphin fountain on the grass beside the blank landscape's road and keeps a close
// screenshot of each: their streams of water should show as arcs. A second look a moment later shows the water in
// their basins moved on.
//
//   ./nob script scripts/tests/fountains.ts -- --no-audio

export {};

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(
    () => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined,
    1200,
    "the build lists",
);
const paths = map.paths();
const gate = paths.reduce((a, b) => (b.row > a.row ? b : a));
const fountains = [
    { symbol: "CupidFountain", col: gate.col + 4, row: gate.row - 5 },
    { symbol: "DolphinFountain", col: gate.col - 4, row: gate.row - 5 },
];
for (const f of fountains) {
    camera.look({ col: f.col + 0.5, row: f.row + 0.5 }, { col: f.col + 0.5, row: f.row + 9.5, up: 20 });
    await frames(3);
    expect(build.pick("Style/Vanilla/Style", f.symbol), `${f.symbol} in the scenery lists`);
    await frames(2);
    const at = map.screen(f.col + 0.5, f.row + 0.5);
    expect(at !== undefined, `${f.symbol}'s tile in sight`);
    pointer.move(at!.x, at!.y);
    await frames(3);
    expect(build.placing()?.problem === "none", `${f.symbol} placeable, not ${build.placing()?.problem}`);
    await pointer.click(at!.x, at!.y);
    build.stop();
    await frames(1);
}
for (const f of fountains) {
    camera.look({ col: f.col + 0.5, row: f.row + 0.5, up: 2 }, { col: f.col + 0.5, row: f.row + 3.5, up: 4 });
    await frames(60);
    await screenshot(f.symbol);
    await park.advance(1);
    await frames(2);
    await screenshot(`${f.symbol}_later`);
}
