// SPDX-License-Identifier: GPL-3.0-or-later
//
// The land tools on the game's blank landscape: digs a pit with Shape ground, paints round it with Paint
// ground, fills it with Water by clicking its wall and takes the lake away again, then raises the ground under
// a tree planted there, which rises with it.
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
await until(() => ui.find("tools/Land/Land") !== undefined, 300, "the tool row");
await screenshot("opened");

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
// From high up and close to overhead, so the bottom of the pit stays in sight past its rim: a tile dug a
// tile deep hides its floor from any view flatter than its walls are high.
camera.look({ col: col + 0.5, row: row + 0.5 }, { col: col - 1, row: row - 2, up: 60 });
await frames(2);

const lakes = map.water().length;
const before = low(col, row);
const cash = park.cash();

// Aimed again before each drag: the camera may scroll while the pointer is in the tool row by the edge.
await pick("Land_Brush_Height");
const [x, y] = on_view(col, row);

await pointer.drag([x, y], [x, y + 0.1], 40);

log("dug a pit:", before, "->", low(col, row), "cash", cash, "->", park.cash());

await screenshot("pit");

expect(low(col, row) < before, "the tile to be lower");
expect(cash === undefined || park.cash()! < cash, "the digging to be paid for");

const surface = map.tile(col + 2, row)!.surface;

await pick("Land_Brush_Paint");
await until(() => ui.find("land_panel/body/Land_Paint_Base2") !== undefined, 60, "the land panel to show the surfaces");

expect(ui.find("land_panel/Land_Brush_Radius") !== undefined, "the land panel to show the brush size");

await ui.click("land_panel/body/Land_Paint_Base2");
await pointer.drag(on_view(col - 2, row), on_view(col + 2, row), 30);

log("painted:", surface, "->", map.tile(col + 2, row)!.surface);

await screenshot("painted");

expect(map.tile(col + 2, row)!.surface !== surface, "the ground beside the pit to be painted");

await pick("Land_Water");

// A click on flat ground at a whole height holds no water.
const [fx, fy] = on_view(col + 2, row);
await pointer.click(fx, fy);
await frames(2);
expect(map.water().length === lakes, "no water from a click on flat ground");

// A click on the pit's far wall, halfway up: the lake rises to that height, rounded up.
const foot = map.screen(col + 0.5, row + 0.98)!;
const top = map.screen(col + 0.5, row + 1.02)!;
await pointer.click((foot.x + top.x) / 2, (foot.y + top.y) / 2);
await frames(2);
const now = map.water();
log("lakes", lakes, "->", now.length, JSON.stringify(now[now.length - 1]), "level", map.tile(col, row)?.water);
await screenshot("water");
expect(now.length === lakes + 1, "a new lake in the pit");
const level = map.tile(col, row)?.water ?? -1e9;
expect(level > low(col, row) && level <= 0 && Number.isInteger(level), "the lake's level a whole unit between the pit's floor and its rim");

// A click on the lake takes it away.
const [lx, ly] = on_view(col, row);
await pointer.click(lx, ly);
await frames(2);
log("lakes after clicking the lake:", map.water().length);
await screenshot("water_removed");
expect(map.water().length === lakes, "the lake taken away");

// Brushes: a hill raised beside the pit, then a crater sunk on the other side.
function high(col: number, row: number): number {
    return Math.max(...map.tile(col, row)!.corners);
}

const hill_at = col + 4;
const hill_before = high(hill_at, row);

await pick("Land_Brush_Height");

await until(() => ui.find("land_panel/Land_Brush_Height_Raise") !== undefined, 60, "the land panel's raise tab");

await ui.click("land_panel/Land_Brush_Height_Raise");

await until(() => ui.find("land_panel/body/Land_Brush_Height_Raise_Hill") !== undefined, 60, "the hill brush");

await ui.click("land_panel/body/Land_Brush_Height_Raise_Hill");

const [hx, hy] = on_view(hill_at, row);
await pointer.drag([hx, hy], [hx, hy - 0.15], 40);

log("hill:", hill_before, "->", high(hill_at, row));

await screenshot("hill");

expect(high(hill_at, row) > hill_before, "the hill to rise");

const crater_at = col - 4;

camera.look({ col: crater_at + 0.5, row: row + 0.5 }, { col: crater_at - 3, row: row - 6, up: 40 });

await frames(2);
const crater_before = low(crater_at, row);
await ui.click("land_panel/Land_Brush_Height_Lower");
await until(() => ui.find("land_panel/body/Land_Brush_Height_Lower_Crater") !== undefined, 60, "the crater brush");
await ui.click("land_panel/body/Land_Brush_Height_Lower_Crater");

const [cx, cy] = on_view(crater_at, row);

await pointer.drag([cx, cy], [cx, cy + 0.15], 40);

log("crater:", crater_before, "->", low(crater_at, row));

await screenshot("crater");

expect(low(crater_at, row) < crater_before, "the crater to sink");

// Smoothing: averaging wears the hill down while the button is held.
camera.look({ col: hill_at + 0.5, row: row + 0.5 }, { col: hill_at - 3, row: row - 6, up: 40 });
await frames(2);
const hill_top = high(hill_at, row);
await ui.click("land_panel/Land_Brush_Height_Smooth");
await until(() => ui.find("land_panel/body/Land_Brush_Height_Smooth_Average") !== undefined, 60, "the averaging brush");
await ui.click("land_panel/body/Land_Brush_Height_Smooth_Average");
const [sx, sy] = on_view(hill_at, row);
await pointer.drag([sx, sy], [sx + 0.01, sy], 40);
log("smoothed hill:", hill_top, "->", high(hill_at, row));
await screenshot("smoothed");
expect(high(hill_at, row) < hill_top, "the hill to wear down");

// Cliff faces: painted on the tiles under the brush, like surfaces.
await pick("Land_Brush_Paint");
await until(() => ui.find("land_panel/Land_Brush_Paint_Cliff") !== undefined, 60, "the cliff tab");
await ui.click("land_panel/Land_Brush_Paint_Cliff");
await until(() => ui.find("land_panel/body/Land_Paint_Cliff3") !== undefined, 60, "the cliff faces");
await ui.click("land_panel/body/Land_Paint_Cliff3");
const cliff_before = map.tile(hill_at, row)!.cliff;
const [px, py] = on_view(hill_at, row);
await pointer.drag([px, py], [px + 0.01, py], 10);
log("cliff:", cliff_before, "->", map.tile(hill_at, row)!.cliff);
expect(map.tile(hill_at, row)!.cliff !== cliff_before, "the cliff face to change");

// A tree does not lock its tile: the ground rises under it and the tree is seated on the new height.
const tree_at = col + 4;
const tree_row = row + 4;
camera.look({ col: tree_at + 0.5, row: tree_row + 0.5 }, { col: tree_at - 3, row: tree_row - 6, up: 40 });
await frames(2);
await pick("Land_Brush_Paint"); // Put away
const trees = map.objects().length;
expect(build.pick("Style/Vanilla/Style", "Common_Linden_2"), "a tree in the scenery lists");
await frames(2);
const [tx, ty] = on_view(tree_at + 0.5, tree_row + 0.5);
pointer.move(tx, ty);
await frames(3);
log("planting:", JSON.stringify(build.placing()));
await pointer.click(tx, ty);
await frames(2);
const planted = map.objects().slice(trees).find((o) => o.symbol === "Common_Linden_2");
expect(planted !== undefined, "the tree planted");
const tree = planted!;
const tree_ground = low(tree.col, tree.row);
await pick("Land_Brush_Height");
await until(() => ui.find("land_panel/Land_Vertex") !== undefined, 60, "the land panel's tile tab");
await ui.click("land_panel/Land_Vertex");
await until(() => ui.find("land_panel/body/Land_Vertex_Neighbour") !== undefined, 60, "the tile tools");
await ui.click("land_panel/body/Land_Vertex_Neighbour");
const [gx, gy] = on_view(tree.col + 0.5, tree.row + 0.5);
await pointer.drag([gx, gy], [gx, gy - 0.1], 40);
await pick("Land_Brush_Height"); // Put away, which ends the stroke
await frames(2);
const seated = map.objects().find((o) => o.symbol === "Common_Linden_2" && o.col === tree.col && o.row === tree.row);
log("tree tile:", tree_ground, "->", low(tree.col, tree.row), "tree height", tree.height, "->", seated?.height);
await screenshot("tree_raised");
expect(low(tree.col, tree.row) > tree_ground, "the ground under the tree to rise");
expect(seated !== undefined && seated.height !== null && seated.height >= Math.ceil(low(tree.col, tree.row)), "the tree seated on the raised ground");
