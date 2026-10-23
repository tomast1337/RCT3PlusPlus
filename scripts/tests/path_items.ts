// SPDX-License-Identifier: GPL-3.0-or-later
//
// Places the things guests use on paths, as a player would, on the game's blank landscape: a bench, a bin, a
// lamp and two signs. Each must be refused on the grass, go on its own tile of the road in from the park's
// entrance, and then be refused where it already stands.
//
//   ./nob script scripts/tests/path_items.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "the build lists");
const paths = map.paths();
const gate = paths.reduce((a, b) => (b.row > a.row ? b : a));
const symbols = ["Bench", "Bin", "LampVer1", "SignSquare", "Signsmall"];
// The road in from the entrance, clear of the entrance itself, a tile for each item.
const road = paths.filter((p) => p.col === gate.col && p.row < gate.row - 1).sort((a, b) => b.row - a.row).slice(0, symbols.length);
expect(road.length === symbols.length, `${symbols.length} tiles of road, not ${road.length}`);
camera.look({ col: gate.col + 0.5, row: gate.row - 4.5 }, { col: gate.col + 0.5, row: gate.row + 8.5, up: 30 });
await frames(3);

/** What placing shows with the pointer resting on tile `col`, `row`. */
async function hover(col: number, row: number): Promise<{ x: number; y: number; problem: string }> {
    const at = map.screen(col, row);
    expect(at !== undefined, `tile ${col},${row} in sight`);
    pointer.move(at!.x, at!.y);
    await frames(3);
    return { ...at!, problem: build.placing()?.problem ?? "nothing placing" };
}

for (const [i, symbol] of symbols.entries()) {
    const tile = road[i];
    expect(build.pick("Style/Vanilla/Style", symbol), `${symbol} in the scenery lists`);
    await frames(2);
    const grass = await hover(tile.col + 3, tile.row);
    const path = await hover(tile.col, tile.row);
    log(symbol, "on grass", grass.problem, "on the path", path.problem);
    expect(grass.problem === "off path", `${symbol} refused on the grass`);
    expect(path.problem === "none", `${symbol} placeable on the path`);
    await pointer.click(path.x, path.y);
    const again = await hover(tile.col, tile.row);
    expect(again.problem === "blocked", `${symbol} refused where it now stands, not ${again.problem}`);
    build.stop();
    await frames(1);
}
await screenshot("path_items");
quit(0);
