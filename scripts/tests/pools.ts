// SPDX-License-Identifier: GPL-3.0-or-later
//
// Swimming pools, in a park with money. The pools button opens the pool panel; a drag with its tiles paves a rectangle, paid for; a drag
// with its water inside it digs a pool there, walled round where it meets the tiles. A lone square, strips and
// an inner corner show the other walls. Extras go in, and a changing room beside the pool opens the complex.
// The delete tool takes a row up again, money back. Saved and loaded, the pools are back.
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

// The rides menu's pools button opens the pool panel on its editor, water out; its other tabs hold the extras
// and the changing rooms.
const rides = `tools/Rides/${ui.children("tools/Rides")![0]}`;
await until(() => build.flatRides() !== undefined, 1200, "the build items");
if (!ui.find("tools/Rides/Obj_Pools"))
    await ui.click(rides);
await until(() => ui.find("tools/Rides/Obj_Pools") !== undefined, 120, "the rides menu");
await ui.click("tools/Rides/Obj_Pools");
await until(() => ui.find("pool_panel/body/editor/Pool_PlaceWater") !== undefined, 120, "the pool panel");
expect(ui.find("build_list") === undefined, "no build list for pools");
await frames(3);
await screenshot("pool_panel_editor");
await ui.click("pool_panel/Pool_ExtraPanel");
await until(() => ui.find("pool_panel/body/Icon_Default_Jacuzzi") !== undefined, 120, "the extras page");
expect(ui.find("pool_panel/body/Icon_Default_HighDive") !== undefined, "the high diving board on the extras page");
await screenshot("pool_panel_extras");
await ui.click("pool_panel/Pool_ChangingRoomPanel");
await frames(3);
expect(ui.children("pool_panel/body")!.length > 1, `changing rooms on their page, not ${JSON.stringify(ui.children("pool_panel/body"))}`);
await screenshot("pool_panel_rooms");
await ui.click("pool_panel/Pool_PoolPanel");
await frames(2);

// Tiles eight by eight, laid with the panel, then water six by six inside them.
let cash = park.cash()!;
await ui.click("pool_panel/body/editor/Pool_PlaceTiles");
await frames(2);
// The square under the pointer is laid to be seen, and the drag's rectangle while it lasts, unpaid till let go.
pointer.move(...on_view(c0 + 0.5, r0 + 0.5));
await frames(3);
expect(count("SolidTop") === 1, `the square under the pointer shown, not ${count("SolidTop")}`);
pointer.press();
await frames(2);
// Aimed at the middle of the far square: its corners are dug with the squares shown beside it.
pointer.move(...on_view(c0 + 7.5, r0 + 7.5));
await frames(4);
await screenshot("pool_laying");
expect(count("SolidTop") === 64, `the rectangle shown while dragging, not ${count("SolidTop")}`);
expect(park.cash()! >= cash - 100, "nothing paid before letting go");
pointer.release();
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
expect(count("Corner1Top") === 4 && count("EdgeTop") === 16, `four corners and sixteen edges, not ${count("Corner1Top")} and ${count("EdgeTop")}`);
expect(map.tile(c0 + 3, r0 + 3)!.corners.every((h) => h < map.tile(c0 - 1, r0 - 1)!.corners[0]), "the ground dug out under the pool");
await screenshot("pool_built");
camera.look({ col: c0 + 1.5, row: r0 + 1.5 }, { col: c0 + 4.5, row: r0 + 5, up: 2.5 });
await frames(10);
await screenshot("pool_corner");

// Extras: a lamp on a paved square, a ladder on the pool's wall, a second lamp refused where the first stands,
// sun loungers taking a whole paved square, a shower refused beside them.
const extras = () => pieces().filter((o) => !o.symbol.match(/^(Solid|Edge|Corner|End|Full|Base|Railing)/)).length;
const put = async (symbol: string, col: number, row: number) => {
    expect(build.pick(kPool, symbol), `${symbol} in the pool list`);
    await pointer.click(...on_view(col, row));
    await frames(3);
};
cash = park.cash()!;
await put("PoolLamp", c0, r0 + 3);
expect(extras() === 1 && park.cash()! < cash, "a lamp, paid for");
// Turned a quarter to the left, against the pool's wall.
keys.press(",");
await frames(2);
await put("Ladder", c0 + 1.5, r0 + 3.5);
expect(extras() === 2, "a ladder on the wall");
keys.press(".");
await frames(2);
await put("PoolLamp", c0, r0 + 3);
expect(extras() === 2, "no second lamp where one stands");
await put("SunLoungers", c0 + 3, r0);
await put("PoolShower", c0 + 3, r0);
expect(extras() === 3, `loungers alone on their square, not ${extras()} extras`);
// A high diving board on the far paved side, turned to face the water, raised by dragging up from its square;
// facing away from the water it is refused.
expect(build.pick(kPool, "SegmentTypeHighDive"), "the high diving board in the pool list");
camera.look({ col: c0 + 6, row: r0 + 4 }, { col: c0 + 3, row: r0 + 4, up: 4 });
await frames(10);
cash = park.cash()!;
const dive = on_view(c0 + 7.5, r0 + 4.5);
await pointer.drag(dive, [dive[0], dive[1] - 0.08], 8);
await frames(3);
expect(count("DBTopBoard") === 0, "no board facing away from the water");
keys.press(",");
await frames(2);
await pointer.drag(dive, [dive[0], dive[1] - 0.08], 8);
await frames(3);
expect(count("DBTopBoard") === 1 && count("DBMidBoard") >= 1, `a board raised by the drag, not ${count("DBMidBoard")} levels below its top`);
expect(park.cash()! < cash, "the board paid for");
camera.look({ col: c0 + 7.5, row: r0 + 4.5, up: 2 }, { col: c0 + 3.5, row: r0 + 2.5, up: 4 });
await frames(10);
await screenshot("pool_high_dive");
keys.press(".");
await frames(2);
build.stop();
camera.look({ col: c0 + 1.5, row: r0 + 2.5 }, { col: c0 + 4, row: r0 + 6, up: 3 });
await frames(10);
await screenshot("pool_extras");

// A changing room on the ground beside the paved ring, opening on it: the railing there opens. Picked in the
// panel, it follows the pointer as a ghost, shown in the panel's disc too, and turns with the turn keys; placed,
// the tool goes and the room's window opens, where the pool complex opens and its ticket price is set.
const room = () => map.objects().find((o) => o.overlay !== kPool && o.col === c0 - 1 && o.row === r0 + 4);
const railings = count("Railing");
if (!ui.find("tools/Rides/Obj_Pools"))
    await ui.click(rides);
await until(() => ui.find("tools/Rides/Obj_Pools") !== undefined, 120, "the rides menu");
await ui.click("tools/Rides/Obj_Pools");
await until(() => ui.find("pool_panel/Pool_ChangingRoomPanel") !== undefined, 120, "the pool panel");
await ui.click("pool_panel/Pool_ChangingRoomPanel");
await frames(2);
await ui.click(`pool_panel/body/${ui.children("pool_panel/body")![0]}`);
camera.look({ col: c0 - 0.5, row: r0 + 4.5 }, { col: c0 + 3, row: r0 + 5.5, up: 2.5 });
await frames(10);
// It opens towards the pool once turned a quarter.
pointer.move(...on_view(c0 - 0.5, r0 + 4.5));
await frames(3);
await screenshot("pool_room_ghost_turned_away");
await pointer.click(...on_view(c0 - 0.5, r0 + 4.5));
await frames(3);
expect(room() === undefined, "no changing room opening away from the pool");
keys.press(".");
await frames(3);
await pointer.click(...on_view(c0 - 1.5, r0 + 4.5));
await frames(3);
expect(room() === undefined, "no changing room away from the pool");
pointer.move(...on_view(c0 - 0.5, r0 + 4.5));
await frames(5);
await screenshot("pool_room_ghost");
await pointer.click(...on_view(c0 - 0.5, r0 + 4.5));
await frames(3);
expect(room() !== undefined, "a changing room beside the pool");
expect(count("Railing") === railings - 1, `the railing open towards it, not ${railings - count("Railing")} fewer`);
await until(() => ui.find("info") !== undefined, 120, "the changing room's window");
expect(ui.find("pool_panel") === undefined, "the pool panel gone, the room placed once");
await frames(5);
await screenshot("pool_changing_room");
// Its window is an attraction's: details naming the complex, its mode, its money, its customers and upkeep.
expect(JSON.stringify(ui.children("info")).includes("Attraction_OpMode"), `the attraction tabs, not ${JSON.stringify(ui.children("info"))}`);
await ui.click("info/body/PoolComplexName");
keys.press("Ctrl+A");
keys.type("Lagoon");
keys.press("Enter");
await frames(3);
expect(ui.find("info/body/PoolComplexName")?.text === "Lagoon", `the complex named, not ${ui.find("info/body/PoolComplexName")?.text}`);
await screenshot("pool_room_details");
await ui.click("info/Attraction_OpMode");
await frames(3);
// Dragged from the choice down onto its first row, Open.
const choice = ui.find("info/body/AttractionStatusOpen")!;
await pointer.drag([choice.x, choice.y], [choice.x, choice.bottom + (choice.bottom - choice.top) / 2], 6);
await frames(3);
await screenshot("pool_complex_open");
for (const tab of ["Attraction_Finances", "Attraction_Customers", "Attraction_Maintenance"])
{
    await ui.click(`info/${tab}`);
    await frames(3);
    await screenshot(`pool_room_${tab}`);
}
await ui.click("info/close");

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
// Put away, so the square under the pointer is not shown laid as well.
build.stop();
await frames(2);
await screenshot("pool_shapes");
camera.look({ col: c0 + 4, row: r0 + 11.5 }, { col: c0 + 4, row: r0 + 17, up: 6 });
await frames(10);
await screenshot("pool_shapes_close");
camera.look({ col: c0 + 7, row: r0 + 11 }, { col: c0 + 8, row: r0 + 13.5, up: 5 });
await frames(10);
await screenshot("pool_inner_corner");
expect(count("FullTop") === 1, `a lone square walled all round, not ${count("FullTop")}`);
expect(count("EndTop") === 3, `the strip's two ends and the long strip's far end, not ${count("EndTop")}`);
expect(count("CurveCorner1Top") === 1, `the rounded inner corner, not ${count("CurveCorner1Top")}`);

// The first pool laid again with loose curves from the panel: its corners sweep on into the squares beside
// them, for nothing. Then a tight curve on one corner, and one cut across.
const open_editor = async () => {
    build.stop();
    if (!ui.find("tools/Rides/Obj_Pools"))
        await ui.click(rides);
    await until(() => ui.find("tools/Rides/Obj_Pools") !== undefined, 120, "the rides menu");
    await ui.click("tools/Rides/Obj_Pools");
    await until(() => ui.find("pool_panel/Pool_PoolPanel") !== undefined, 120, "the pool panel");
    // It opens on the page last shown.
    const on_editor = () => ui.children("pool_panel/body")?.includes("editor") === true;
    if (!on_editor())
        await ui.click("pool_panel/Pool_PoolPanel");
    await until(on_editor, 120, "the pool panel's editor");
};
const relay = async (edge: string, a: [number, number], b: [number, number]) => {
    await ui.click(`pool_panel/body/editor/${edge}`);
    await frames(2);
    await pointer.drag(on_view(...a), on_view(...b), 6);
    await frames(3);
};
camera.look({ col: c0 + 4, row: r0 + 4 }, { col: c0 + 4, row: r0 + 16, up: 14 });
await frames(10);
await open_editor();
await ui.click("pool_panel/body/editor/Pool_DepthNormal");
await frames(2);
cash = park.cash()!;
await relay("Pool_CurveLarge", [c0 + 1, r0 + 1], [c0 + 6, r0 + 6]);
expect(count("CurveCorner9Top") === 4, `four sweeping corners, not ${count("CurveCorner9Top")}`);
expect(count("CurveEdge1Top") === 4 && count("CurveEdge2Top") === 4, `the edges meeting them, not ${count("CurveEdge1Top")} and ${count("CurveEdge2Top")}`);
// The park runs meanwhile, a few cents either way.
expect(Math.abs(park.cash()! - cash) < 100, `a new edge free, not ${park.cash()! - cash}`);
await relay("Pool_CurveSmall", [c0 + 6, r0 + 6], [c0 + 6, r0 + 6]);
await relay("Pool_CurveDiagonal", [c0 + 1, r0 + 6], [c0 + 1, r0 + 6]);
expect(count("CurveCorner9Top") === 2 && count("Corner2Top") === 1, `two sweeps left and a corner cut across, not ${count("CurveCorner9Top")} and ${count("Corner2Top")}`);
expect(count("CurveCorner5Top") + count("CurveCorner6Top") + count("CurveCorner7Top") === 1, "a tight curve");
build.stop();
await frames(2);
camera.look({ col: c0 + 3.5, row: r0 + 4.5 }, { col: c0 + 3.5, row: r0 + 9, up: 20 });
await frames(10);
await screenshot("pool_curves");
await open_editor();
await ui.click("pool_panel/body/editor/Pool_CurveStraight");
await frames(2);
build.stop();

// The panel's delete tool takes the last row of the first pool's tiles up again: they go while the drag lasts,
// the money back showing at the pointer, and are paid back when it is let go.
camera.look({ col: c0 + 4, row: r0 + 4 }, { col: c0 + 4, row: r0 + 16, up: 14 });
await frames(10);
cash = park.cash()!;
const paved = count("SolidTop");
await open_editor();
expect(ui.find("pool_panel/body/editor/Pool_PlaceTiles") !== undefined, "the editor as it was left");
await ui.click("pool_panel/body/editor/Pool_DeleteArea");
await frames(2);
pointer.move(...on_view(c0, r0 + 7));
await frames(2);
pointer.press();
await frames(2);
pointer.move(...on_view(c0 + 7, r0 + 7));
await frames(4);
expect(count("SolidTop") < paved, `the row gone while dragging, ${count("SolidTop")} of ${paved} left`);
const during = park.cash()!;
await screenshot("pool_deleting");
pointer.release();
await frames(3);
build.stop();
expect(park.cash()! > during, "the tiles paid back when let go");

// Tiles the park cannot pay for go back when let go, and a message says why.
const before_broke = park.cash()!;
park.setCash(1);
expect(build.pick(kPool, "Pool_PlaceTiles"), "the tiles in the pool list");
const unpaved = count("SolidTop");
await pointer.drag(on_view(c0, r0 + 7), on_view(c0 + 3, r0 + 7), 6);
await frames(3);
build.stop();
await frames(2);
expect(count("SolidTop") === unpaved, `no tiles laid without the money, ${count("SolidTop")} not ${unpaved}`);
expect(park.messages().some((m) => m.key === "PlaceObject_NotEnoughMoney"), "the not enough money message");
park.setCash(before_broke);
await frames(10);
await screenshot("pool_cut");

// Saved and loaded again: the pools come back, without the square the tiles tool shows under the pointer.
const kept = pieces().length;
expect(build.pick(kPool, "Pool_PlaceTiles"), "the tiles in the pool list");
pointer.move(...on_view(c0 + 1.5, r0 + 7.5));
await frames(3);
expect(pieces().length > kept, "the square under the pointer shown before saving");
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
await until(() => park.loaded() && pieces().length > 0, 1200, "the pools back after loading");
await frames(3);
expect(pieces().length === kept, `${kept} pool pieces back after loading, not ${pieces().length}`);
expect(room() !== undefined, "the changing room back after loading");
camera.look({ col: c0 + 4, row: r0 + 4 }, { col: c0 + 4, row: r0 + 16, up: 14 });
await frames(10);
await screenshot("pool_loaded");

// The changing room's window again, its complex's name kept; selling the complex takes up its squares, extras
// and room, once asked, and leaves the pool below.
const first = () => pieces().filter((o) => o.row < r0 + 8).length;
const below = pieces().length - first();
camera.look({ col: c0 - 0.5, row: r0 + 4.5 }, { col: c0 + 3, row: r0 + 5.5, up: 2.5 });
await frames(10);
await pointer.click(...on_view(c0 - 0.5, r0 + 4.5));
await until(() => ui.find("info/Guest_Rides") !== undefined, 120, "the changing room's window");
await ui.click("info/Guest_Rides");
await until(() => ui.find("info/body/PoolComplexName") !== undefined, 120, "the changing room's details");
expect(ui.find("info/body/PoolComplexName")?.text === "Lagoon", "the complex's name kept");
cash = park.cash()!;
await ui.click("info/body/GUI2_DeleteSelection");
await until(() => ui.find("sell_pool_question") !== undefined, 120, "the question");
await screenshot("pool_sell_question");
await ui.click("sell_pool_question/content/GUI2MiniDialog_Confirm");
await frames(3);
expect(first() === 0 && room() === undefined, `the complex gone, not ${first()} pieces`);
expect(pieces().length === below, "the pool below kept");
expect(park.cash()! > cash, "money back for the complex");
