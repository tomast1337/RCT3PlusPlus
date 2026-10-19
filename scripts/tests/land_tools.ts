// SPDX-License-Identifier: GPL-3.0-or-later
//
// The land tools in a park: digs a pit with Shape ground, paints round it with Paint ground and fills it with
// Water. Run on a park with open, owned, flat ground under the lower left of the first view, such as the Fun
// Valley fixture:
//
//   ./nob script scripts/tests/land_tools.ts -- --park "Fun Valley Amusment park.dat" --no-simulate --seed 1

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

/** All the corner heights of the map added up. */
function ground(): number {
    const { width, height } = map.size();
    let sum = 0;
    for (let row = 0; row < height; ++row)
        for (let col = 0; col < width; ++col)
            sum += map.tile(col, row)!.corners.reduce((a, b) => a + b, 0);
    return sum;
}

const spot: [number, number] = [0.3, 0.85];
const down: [number, number] = [0.3, 0.95];
const up: [number, number] = [0.3, 0.8];

await park.ready();
await frames(30); // The interface settles in
const cash = park.cash();
const lakes = map.water().length;
const before = ground();

await pick("Land_Brush_Height");
await pointer.drag(spot, down, 40);
log("dug a pit; ground", before, "->", ground(), "cash", cash, "->", park.cash());
expect(ground() < before, "the ground to be lower");
expect(cash === undefined || park.cash()! < cash, "the digging to be paid for");

await pick("Land_Brush_Paint");
expect(ui.find("paint_ground/content/Land_Paint_Base2") !== undefined, "the paint window to show the surfaces");
await ui.click("paint_ground/content/Land_Paint_Base2");
await pointer.drag([0.2, 0.9], [0.4, 0.9], 30);

await pick("Land_Water");
await pointer.drag(spot, up, 40);
const now = map.water();
log("lakes", lakes, "->", now.length, JSON.stringify(now[now.length - 1]));
expect(now.length === lakes + 1, "a new lake in the pit");

await screenshot();
