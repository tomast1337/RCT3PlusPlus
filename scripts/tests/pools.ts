// SPDX-License-Identifier: GPL-3.0-or-later
//
// Swimming pools, in a park with money. A drag with the pool list's tiles paves a rectangle, paid for; a drag
// with its water inside it digs a pool there, walled round where it meets the tiles. A lone square, strips and
// an inner corner show the other walls. The delete tool takes a row up again, money back. Saved and loaded, the
// pools are back.
//
//   ./nob script scripts/tests/pools.ts -- --no-intro --no-audio --no-simulate --seed 1

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

function on_view(col: number, row: number): [number, number] {
    const at = map.screen(col, row);
    if (!at)
        throw new Error(`tile ${col}, ${row} is not in sight`);
    return [at.x, at.y];
}

const kPool = "Pool/Default/Default_Stub";

await park.open("Campaigns/Base/VanillaHills.dat");
await until(() => ui.find("tools/Land/Land") !== undefined, 300, "the tool row");

// A ten by fourteen rectangle of the park's own land, near flat, with nothing on it.
const { width, height } = map.size();
const taken = new Set([...map.objects(), ...map.paths()].map((o) => `${o.col},${o.row}`));
const clear = (c: number, r: number) => {
    const level = map.tile(c, r)?.corners[0] ?? 0;
    for (let row = r - 1; row < r + 15; ++row)
        for (let col = c - 1; col < c + 11; ++col)
        {
            const tile = map.tile(col, row);
            if (!tile?.owned || tile.water !== undefined || tile.corners.some((h) => Math.abs(h - level) > 1) || taken.has(`${col},${row}`))
                return false;
        }
    return true;
};
let site: { col: number; row: number } | undefined;
for (let r = 1; r < height - 16 && !site; r += 2)
    for (let c = 1; c < width - 12 && !site; c += 2)
        if (clear(c, r))
            site = { col: c, row: r };
expect(site !== undefined, "room for a pool");
const c0 = site!.col;
const r0 = site!.row;
log("pool at", c0, r0);
// The pool pieces on the site; the park has pools of its own elsewhere.
const pieces = () => map.objects().filter((o) => o.overlay === kPool && o.col >= c0 && o.col < c0 + 10 && o.row >= r0 && o.row < r0 + 14);
const count = (symbol: string) => pieces().filter((o) => o.symbol === symbol).length;
camera.look({ col: c0 + 4, row: r0 + 4 }, { col: c0 + 4, row: r0 + 16, up: 14 });
await frames(10);

// Tiles eight by eight, then water six by six inside them.
let cash = park.cash()!;
expect(build.pick(kPool, "Pool_PlaceTiles"), "the tiles in the pool list");
await pointer.drag(on_view(c0, r0), on_view(c0 + 7, r0 + 7), 8);
await frames(3);
expect(count("SolidTop") === 64, `64 paved squares, not ${count("SolidTop")}`);
expect(count("Railing") === 32, `a railing round them, not ${count("Railing")}`);
expect(park.cash()! < cash, "the tiles paid for");
cash = park.cash()!;
expect(build.pick(kPool, "Pool_DepthNormal"), "the water in the pool list");
await pointer.drag(on_view(c0 + 1, r0 + 1), on_view(c0 + 6, r0 + 6), 8);
await frames(3);
expect(park.cash()! < cash, "the water paid for");
expect(count("Base") === 36, `36 squares of water, not ${count("Base")}`);
expect(count("SolidTop") === 28, `28 tiles left round it, not ${count("SolidTop")}`);
expect(count("Corner2Top") === 4 && count("EdgeTop") === 16, `four corners and sixteen edges, not ${count("Corner2Top")} and ${count("EdgeTop")}`);
expect(map.tile(c0 + 3, r0 + 3)!.corners.every((h) => h < map.tile(c0 - 1, r0 - 1)!.corners[0]), "the ground dug out under the pool");
await screenshot("pool_built");
camera.look({ col: c0 + 1.5, row: r0 + 1.5 }, { col: c0 + 4.5, row: r0 + 5, up: 2.5 });
await frames(10);
await screenshot("pool_corner");

// Below it, in tiles: a lone square, a strip three long, and a strip four long with a block under its first
// two squares, an inner corner where they meet.
expect(build.pick(kPool, "Pool_PlaceTiles"), "the tiles in the pool list");
camera.look({ col: c0 + 4.5, row: r0 + 11 }, { col: c0 + 4.5, row: r0 + 22, up: 12 });
await frames(10);
await pointer.drag(on_view(c0, r0 + 9), on_view(c0 + 9, r0 + 13), 8);
await frames(3);
expect(build.pick(kPool, "Pool_DepthDeep"), "the deep water in the pool list");
for (const [a, b] of [[[c0 + 1, r0 + 11], [c0 + 1, r0 + 11]], [[c0 + 3, r0 + 10], [c0 + 3, r0 + 12]], [[c0 + 5, r0 + 10], [c0 + 8, r0 + 10]], [[c0 + 5, r0 + 11], [c0 + 6, r0 + 12]]] as [number, number][][])
{
    await pointer.drag(on_view(a[0], a[1]), on_view(b[0], b[1]), 6);
    await frames(3);
}
await screenshot("pool_shapes");
camera.look({ col: c0 + 4, row: r0 + 11.5 }, { col: c0 + 4, row: r0 + 17, up: 6 });
await frames(10);
await screenshot("pool_shapes_close");
camera.look({ col: c0 + 7, row: r0 + 11 }, { col: c0 + 8, row: r0 + 13.5, up: 5 });
await frames(10);
await screenshot("pool_inner_corner");
expect(count("FullTop") === 1, `a lone square walled all round, not ${count("FullTop")}`);
expect(count("EndTop") === 3, `the strip's two ends and the long strip's far end, not ${count("EndTop")}`);
expect(count("Corner3Top") === 1, `the inner corner, not ${count("Corner3Top")}`);

// The delete tool takes the last row of the first pool's tiles up again, paid back.
camera.look({ col: c0 + 4, row: r0 + 4 }, { col: c0 + 4, row: r0 + 16, up: 14 });
await frames(10);
cash = park.cash()!;
expect(build.pick(kPool, "Pool_DeleteArea"), "the pool delete tool");
await pointer.drag(on_view(c0, r0 + 7), on_view(c0 + 7, r0 + 7), 6);
await frames(3);
build.stop();
expect(park.cash()! > cash, "the tiles paid back");
await frames(10);
await screenshot("pool_cut");

// Saved and loaded again: the pools come back.
const kept = pieces().length;
await ui.click(`tools/Files/${ui.children("tools/Files")![0]}`);
await ui.click("tools/Files/File_SaveGame");
await until(() => ui.find("park_files") !== undefined, 120, "the save window");
await ui.click("park_files/content/open");
await until(() => ui.find("park_files") === undefined, 120, "the save window to close");
if (!ui.find("tools/Files/File_LoadGame"))
    await ui.click(`tools/Files/${ui.children("tools/Files")![0]}`);
await until(() => ui.find("tools/Files/File_LoadGame") !== undefined, 120, "the files menu");
await ui.click("tools/Files/File_LoadGame");
const saved_row = "park_files/content/list/view/content/row0";
await until(() => ui.find(saved_row) !== undefined, 120, "the save in the load window");
await ui.click(saved_row);
await ui.click("park_files/content/open");
await until(() => pieces().length === kept, 1200, `${kept} pool pieces back after loading`);
camera.look({ col: c0 + 4, row: r0 + 4 }, { col: c0 + 4, row: r0 + 16, up: 14 });
await frames(10);
await screenshot("pool_loaded");
