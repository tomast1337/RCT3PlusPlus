// SPDX-License-Identifier: GPL-3.0-or-later
//
// A park from the game with pools, edited with the pool tool: a water square taken out of the middle of a wall, a
// paving square laid beside the deck, one raised, the curves set straight and a pool path laid along the deck. After
// each, the pool pieces shown round the work are those the pools' squares place now (map.poolPieces), each once:
// none the game placed left standing as it was, and the extras the game placed kept. Give it the two pools park:
//
//   ./nob script scripts/tests/pool_edit_saved.ts -- --no-intro --no-audio --no-simulate --seed 1 --park PARK

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
// The pieces the squares are built of, not the extras standing on them.
const kSquarePiece = /^(Solid|Edge|Corner|Curve|End|Full|Base|Railing|Stairs|DB|Jacuzzi|Path|Central)/;
const in_pool = (o: { overlay: string }) => o.overlay.replaceAll("\\", "/").toLowerCase() === kPool.toLowerCase();

await until(() => park.loaded(), 1200, "the park");
await frames(10);

// The smaller pool: water two squares wide, seven long, a deck of paving round it. Found by its west wall.
const base = map.poolPieces().filter((o) => o.symbol === "Base");
const walls = base.filter((o) => !base.some((b) => b.col === o.col - 1 && b.row === o.row));
const pool = walls.find((o) => base.some((b) => b.col === o.col + 1 && b.row === o.row) && !base.some((b) => b.col === o.col + 2 && b.row === o.row) && base.filter((b) => b.col === o.col && Math.abs(b.row - o.row) <= 3).length === 7);
expect(pool !== undefined, "a pool two squares wide");
const c = pool!.col;
const r = pool!.row;
log("pool wall at", c, r);

const extras = () => map.objects().filter((o) => in_pool(o) && !kSquarePiece.test(o.symbol)).length;
const saved_extras = extras();
log("extras the game placed", saved_extras);
expect(saved_extras > 0, "the game's extras standing");
// Those the squares place are placed by them alone; a piece they never place (a spiral staircase, not taken in
// yet) stays as the game placed it.
const laid = new Set(map.poolPieces().map((o) => o.symbol));
const kept = map.objects().filter((o) => in_pool(o) && o.overlay !== kPool && kSquarePiece.test(o.symbol));
log("kept as the game placed them", JSON.stringify([...new Set(kept.map((o) => o.symbol))]));
const left = kept.filter((o) => laid.has(o.symbol));
expect(left.length === 0, `no square piece left as the game placed it, not ${JSON.stringify(left.slice(0, 8))} of ${left.length}`);

// Every square piece shown near the work, as many times as the squares place it.
const key = (o: { symbol: string; col: number; row: number; height: number | null }) => `${o.symbol}@${o.col},${o.row},${o.height}`;
const near = (o: { col: number; row: number }) => Math.abs(o.col - c) <= 6 && Math.abs(o.row - r) <= 8;
const check = (what: string) => {
    const shown = map.objects().filter((o) => in_pool(o) && kSquarePiece.test(o.symbol) && !kept.some((k) => k.col === o.col && k.row === o.row && k.symbol === o.symbol && k.height === o.height) && near(o)).map(key).sort();
    const placed = map.poolPieces().filter((o) => kSquarePiece.test(o.symbol) && near(o)).map(key).sort();
    const stale = shown.filter((k, i) => k !== placed[i]);
    log(what, shown.length, "pieces shown,", placed.length, "placed");
    expect(JSON.stringify(shown) === JSON.stringify(placed), `${what}: the pieces the squares place, each once, not ${JSON.stringify(stale.slice(0, 8))} among ${shown.length} for ${placed.length}`);
};
check("as loaded");
camera.look({ col: c + 1, row: r }, { col: c + 1, row: r + 8, up: 9 });
await frames(10);
await screenshot("pool_edit_saved_before");

const rides = `tools/Rides/${ui.children("tools/Rides")![0]}`;
await until(() => build.flatRides() !== undefined, 1200, "the build items");
if (!ui.find("tools/Rides/Obj_Pools"))
    await ui.click(rides);
await until(() => ui.find("tools/Rides/Obj_Pools") !== undefined, 120, "the rides menu");
await ui.click("tools/Rides/Obj_Pools");
await until(() => ui.find("pool_panel/body/editor/Pool_DeleteArea") !== undefined, 120, "the pool panel");
const at_square = async (tool: string, col: number, row: number) => {
    await ui.click(`pool_panel/body/editor/${tool}`);
    await frames(2);
    await pointer.drag(on_view(col + 0.5, row + 0.5), on_view(col + 0.5, row + 0.5), 2);
    await frames(3);
};

// The pool's curved corners set straight: the water laid again with straight edges.
await ui.click("pool_panel/body/editor/Pool_DepthNormal");
await frames(2);
await ui.click("pool_panel/body/editor/Pool_CurveStraight");
await frames(2);
await pointer.drag(on_view(c + 0.5, r - 2.5), on_view(c + 1.5, r + 3.5), 6);
await frames(3);
expect(!map.poolPieces().some((o) => o.symbol.startsWith("Curve") && near(o)), "no curve left");
check("edges set straight");
// A water square out of the middle of the west wall: the walls beside it close round it.
await at_square("Pool_DeleteArea", c, r);
expect(!map.poolPieces().some((o) => o.symbol === "Base" && o.col === c && o.row === r), "the water square taken up");
check("square taken up");
// A paving square laid against the deck's west side, at the deck's surface where the drag began, the deck's
// railing there gone.
const deck = map.poolPieces().find((o) => o.symbol === "SolidTop" && o.col === c - 1 && o.row === r)!.height!;
await ui.click("pool_panel/body/editor/Pool_PlaceTiles");
await frames(2);
await pointer.drag(on_view(c - 0.5, r + 0.5), on_view(c - 1.5, r + 0.5), 4);
await frames(3);
expect(map.poolPieces().some((o) => o.symbol === "SolidTop" && o.col === c - 2 && o.row === r && o.height === deck), "a square laid beside the deck, level with it");
check("square laid beside");
// A deck square on the east side raised a unit.
await ui.click("pool_panel/body/editor/Pool_Height");
await ui.click("pool_panel/body/editor/height/field");
keys.press("Ctrl+A");
keys.type(`${deck + 1}`);
keys.press("Enter");
await frames(2);
await pointer.drag(on_view(c + 2.5, r + 1.5), on_view(c + 2.5, r + 1.5), 2);
await frames(3);
await ui.click("pool_panel/body/editor/Pool_Height");
expect(map.poolPieces().some((o) => o.symbol === "SolidTop" && o.col === c + 2 && o.row === r + 1 && o.height === deck + 1), "the deck square raised");
check("square raised");
build.stop();
await frames(3);
await screenshot("pool_edit_saved_edited");

// A pool path from the square laid beside the deck, along the deck's west side.
await ui.click("tools/Rides/Obj_Pools");
await until(() => ui.find("pool_panel/Pool_PathPanel") !== undefined, 120, "the pool panel");
await ui.click("pool_panel/Pool_PathPanel");
await until(() => ui.children("pool_panel/body")?.includes("paths") === true, 120, "the paths page");
await ui.toggle("pool_panel/pin", false);
await pointer.drag(on_view(c - 1.5, r + 0.5), on_view(c - 1.5, r - 2.5), 6);
await frames(3);
expect(map.poolPieces().filter((o) => o.symbol.startsWith("Path") && o.col === c - 2 && o.height === deck).length >= 3, "a pool path laid along the deck, level with it");
check("path laid");
await ui.toggle("pool_panel/pin", true);
build.stop();
await frames(3);
expect(extras() === saved_extras, `the game's ${saved_extras} extras kept, not ${extras()}`);
camera.look({ col: c + 1, row: r }, { col: c + 1, row: r + 8, up: 9 });
await frames(10);
await screenshot("pool_edit_saved_after");
camera.look({ col: c, row: r }, { col: c - 4, row: r + 3, up: 4 });
await frames(10);
await screenshot("pool_edit_saved_close");

// Saved and loaded again: the edited pools as they were, their pieces placed once, the game's extras still there.
const all_pieces = () => JSON.stringify(map.objects().filter(in_pool).map((o) => `${o.overlay === kPool}:${key(o)}`).sort());
const edited = all_pieces();
// Not the supports: those built under a pool path as it is laid are not built again when the park loads.
const listed = () => map.objects().filter((o) => !o.overlay.startsWith("Supports")).map((o) => `${o.overlay}:${key(o)}`).sort();
const objects = JSON.stringify(listed());
const click_tool = async (tool: string) => ui.click(`tools/${tool}/${ui.children(`tools/${tool}`)![0]}`);
await click_tool("Files");
await ui.click("tools/Files/File_SaveGame");
await until(() => ui.find("park_files") !== undefined, 120, "the save window");
await ui.click("park_files/content/open");
await until(() => ui.find("park_files") === undefined, 120, "the save window to close");
if (!ui.find("tools/Files/File_LoadGame"))
    await click_tool("Files");
await until(() => ui.find("tools/Files/File_LoadGame") !== undefined, 120, "the files menu");
await ui.click("tools/Files/File_LoadGame");
// Loading asks first whether to save; the park was saved just now.
await until(() => ui.find("load_question") !== undefined, 120, "the question before loading");
await ui.click("load_question/content/answer1");
await until(() => ui.find("park_files/content/list/view/content/row0") !== undefined, 120, "the save in the load window");
await ui.click("park_files/content/list/view/content/row0");
await ui.click("park_files/content/open");
await frames(5);
await until(() => park.loaded(), 1200, "the saved park");
await frames(10);
expect(all_pieces() === edited, "the pool pieces as saved");
expect(JSON.stringify(listed()) === objects, "the other objects as saved");
check("loaded again");
