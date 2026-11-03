// SPDX-License-Identifier: GPL-3.0-or-later
//
// Places a row of jumping fountains and a water cannon along the road in from the blank landscape's entrance,
// brings guests in, and keeps a close screenshot of each: water jumping from fountain to fountain, and the
// cannon turned to spray a guest, its jet arcing down to a splash.
//
//   ./nob script scripts/tests/water_jets.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "the build lists");
const paths = map.paths();
const gate = paths.reduce((a, b) => (b.row > a.row ? b : a));
const road = paths.filter((p) => p.col === gate.col && p.row < gate.row - 1).sort((a, b) => b.row - a.row);
const items = [
    { overlay: "Style/Vanilla/Style", symbol: "PathFountain", tile: road[1] },
    { overlay: "Style/Vanilla/Style", symbol: "PathFountain", tile: road[2] },
    { overlay: "Style/Vanilla/Style", symbol: "PathFountain", tile: road[3] },
    { overlay: "Style/Vanilla/CapeTyphoon/Style", symbol: "WaterCannon", tile: road[7] },
];
for (const item of items) {
    expect(item.tile !== undefined, `road for the ${item.symbol}`);
    camera.look({ col: item.tile.col + 0.5, row: item.tile.row + 0.5 }, { col: item.tile.col + 0.5, row: item.tile.row + 8.5, up: 25 });
    await frames(3);
    expect(build.pick(item.overlay, item.symbol), `${item.symbol} in the scenery lists`);
    await frames(2);
    const at = map.screen(item.tile.col, item.tile.row);
    expect(at !== undefined, `the ${item.symbol}'s tile in sight`);
    pointer.move(at!.x, at!.y);
    await frames(3);
    expect(build.placing()?.problem === "none", `${item.symbol} placeable, not ${build.placing()?.problem}`);
    await pointer.click(at!.x, at!.y);
    build.stop();
    await frames(1);
}
expect(park.pathItems().filter((i) => i.kind === "fountain").length === 3, "three jumping fountains");

const row = road[2];
camera.look({ col: row.col + 0.5, row: row.row + 0.5 }, { col: row.col + 3, row: row.row + 1.5, up: 7 });
await park.advance(2);
for (let i = 0; i < 6; ++i) {
    await park.advance(0.25);
    await frames(2);
    await screenshot(`jumping_${i}`);
}

park.dropParties(40, 3);
await park.advanceUntil(() => people.list().some((p) => p.lingering === "water cannon" && p.col !== undefined), 1800, "a guest firing the cannon");
const cannon = road[7];
camera.look({ col: cannon.col + 0.5, row: cannon.row + 0.5, up: 1 }, { col: cannon.col + 2, row: cannon.row + 2.5, up: 7 });
for (let i = 0; i < 3; ++i) {
    await park.advance(1);
    await frames(60);
    await screenshot(`cannon_${i}`);
}
