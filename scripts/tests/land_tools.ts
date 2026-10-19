// SPDX-License-Identifier: GPL-3.0-or-later
//
// The land tools on the game's blank landscape: digs a pit with Shape ground, paints round it with Paint
// ground and fills it with Water.
//
//   ./nob script scripts/tests/land_tools.ts -- --no-intro --no-audio --no-simulate --seed 1

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

/** Picks a tool from the land menu, opening the menu when it is shut. */
async function pick(icon: string): Promise<void> {
    if (!ui.find(`tools/Land/${icon}`))
        await ui.click("tools/Land/Land");
    await ui.click(`tools/Land/${icon}`);
}

/** Where tile (col, row) is on the view; throws when it is out of sight. */
function on_view(col: number, row: number): [number, number] {
    const at = map.screen(col, row);
    if (!at)
        throw new Error(`tile ${col}, ${row} is not in sight`);
    return [at.x, at.y];
}

function low(col: number, row: number): number {
    return Math.min(...map.tile(col, row)!.corners);
}

await park.open("Campaigns/Base/BlankLandscape.dat");
await frames(30); // The interface settles in

// The owned tile nearest the middle of the map that is in sight.
const { width, height } = map.size();
let col = -1;
let row = -1;
for (let r = 0; r < Math.max(width, height) && col < 0; ++r)
    for (let dc = -r; dc <= r && col < 0; ++dc)
        for (const dr of [-r, r]) {
            const c = (width >> 1) + dc;
            const w = (height >> 1) + dr;
            if (map.tile(c, w)?.owned && map.screen(c, w) && col < 0)
                [col, row] = [c, w];
        }
expect(col >= 0, "an owned tile in sight");
log("working on tile", col, row);

const lakes = map.water().length;
const before = low(col, row);
const cash = park.cash();

await pick("Land_Brush_Height");
const [x, y] = on_view(col, row);
await pointer.drag([x, y], [x, y + 0.1], 40);
log("dug a pit:", before, "->", low(col, row), "cash", cash, "->", park.cash());
expect(low(col, row) < before, "the tile to be lower");
expect(cash === undefined || park.cash()! < cash, "the digging to be paid for");

const surface = map.tile(col + 2, row)!.surface;
await pick("Land_Brush_Paint");
expect(ui.find("paint_ground/content/Land_Paint_Base2") !== undefined, "the paint window to show the surfaces");
await ui.click("paint_ground/content/Land_Paint_Base2");
await pointer.drag(on_view(col - 2, row), on_view(col + 2, row), 30);
log("painted:", surface, "->", map.tile(col + 2, row)!.surface);
expect(map.tile(col + 2, row)!.surface !== surface, "the ground beside the pit to be painted");

await pick("Land_Water");
await pointer.drag(on_view(col, row), [x, y - 0.05], 40);
const now = map.water();
log("lakes", lakes, "->", now.length, JSON.stringify(now[now.length - 1]));
expect(now.length === lakes + 1, "a new lake in the pit");

await screenshot();
