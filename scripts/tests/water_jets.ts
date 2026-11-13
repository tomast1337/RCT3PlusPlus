// SPDX-License-Identifier: GPL-3.0-or-later
//
// Places a row of jumping fountains and a water cannon along the road in from the blank landscape's entrance,
// brings guests in, and keeps screenshots of each, close and far, from the side and above: water jumping from fountain to fountain, and the
// cannon turned to spray a guest, its jet arcing down to a splash.
//
//   ./nob script scripts/tests/water_jets.ts -- --no-audio

export {};

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

// Each is looked at close and from a usual playing distance, from the side and from above.
type At = { col: number; row: number };
function views(at: At, up: number): [string, At & { up: number }][] {
    return [
        ["close_side", { col: at.col + 3.5, row: at.row + 0.5, up: up + 1 }],
        ["close_above", { col: at.col + 1.5, row: at.row + 2, up: up + 8 }],
        ["far_side", { col: at.col + 10, row: at.row + 3, up: up + 6 }],
        ["far_above", { col: at.col + 3, row: at.row + 6, up: up + 30 }],
    ];
}

const row = road[2];
await park.advance(2);
for (const [name, eye] of views(row, 0)) {
    camera.look({ col: row.col + 0.5, row: row.row + 0.5, up: 1 }, eye);
    for (let i = 0; i < 3; ++i) {
        await park.advance(0.3);
        await frames(2);
        await screenshot(`jumping_${name}_${i}`);
    }
}

park.dropParties(40, 3);
await park.advanceUntil(() => people.list().some((p) => p.lingering === "water cannon" && p.col !== undefined), 1800, "a guest firing the cannon");
const cannon = road[7];
for (const [name, eye] of views(cannon, 0)) {
    camera.look({ col: cannon.col + 0.5, row: cannon.row + 0.5, up: 1 }, eye);
    await park.advance(1);
    await frames(60);
    await screenshot(`cannon_${name}`);
}
