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
// The game's eleven: nine extras of the pool type, the high diving board and the spa pool; the ramps and the spiral
// staircase are on the paths page.
const offered_extras = ui.children("pool_panel/body")!;
expect(offered_extras.every((n) => ui.find(`pool_panel/body/${n}`)?.tooltip), "every extra named");
expect(offered_extras.length === 11 && !offered_extras.includes("Icon_Default_SpiralStairs"), `the eleven extras, not ${JSON.stringify(offered_extras)}`);
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
// Against the pool's wall, turned to it unasked.
await put("Ladder", c0 + 1.5, r0 + 3.5);
expect(extras() === 2, "a ladder on the wall");
// A wave machine on the same wall, two squares along, in the corner: the pointer near the wall picks it.
await put("WaveMachine", c0 + 1.2, r0 + 1.5);
expect(count("WaveMachine") === 1, "a wave machine on the wall");
await put("PoolLamp", c0, r0 + 3);
expect(extras() === 3, "no second lamp where one stands");
await put("SunLoungers", c0 + 3, r0);
await put("PoolShower", c0 + 3, r0);
expect(extras() === 4, `loungers alone on their square, not ${extras()} extras`);
// In the far corner of the water, a wall on two sides: a ladder goes against the wall the pointer is nearer,
// a second against the other; a third finds neither free.
camera.look({ col: c0 + 6.5, row: r0 + 6.5 }, { col: c0 + 3.5, row: r0 + 3.5, up: 4 });
await frames(10);
await put("Ladder", c0 + 6.8, r0 + 6.4);
await put("Ladder", c0 + 6.4, r0 + 6.8);
await put("Ladder", c0 + 6.8, r0 + 6.4);
expect(extras() === 6, `two ladders in the corner, one on each wall, not ${extras() - 4}`);
await screenshot("pool_corner_ladders");
// A high diving board on the far paved side, turned to face the water unasked, raised by dragging up from its
// square.
expect(build.pick(kPool, "SegmentTypeHighDive"), "the high diving board in the pool list");
camera.look({ col: c0 + 6, row: r0 + 4 }, { col: c0 + 3, row: r0 + 4, up: 4 });
await frames(10);
cash = park.cash()!;
const dive = on_view(c0 + 7.5, r0 + 4.5);
await pointer.drag(dive, [dive[0], dive[1] - 0.08], 8);
await frames(3);
expect(count("DBTopBoard") === 1 && count("DBMidBoard") >= 1, `a board raised by the drag, not ${count("DBMidBoard")} levels below its top`);
expect(park.cash()! < cash, "the board paid for");
camera.look({ col: c0 + 7.5, row: r0 + 4.5, up: 2 }, { col: c0 + 3.5, row: r0 + 2.5, up: 4 });
await frames(10);
await screenshot("pool_high_dive");
// One on each of the other sides. Each one's boards cover three squares in a row, the tower's in the middle,
// named by the first; their tip reaches out over the water square in front of the tower. The one on the far side
// is dragged up to five levels, as high as the tower a saved park has (pool_dives.ts), to be seen alike from above.
for (const [col, row, up] of [[c0 + 4, r0 + 7, 0.08], [c0 + 5, r0, 0.2], [c0, r0 + 5, 0.08]])
{
    camera.look({ col: col + 0.5, row: row + 0.5 }, { col: col + 0.5, row: row + 3, up: 12 });
    await frames(10);
    const at = on_view(col + 0.5, row + 0.5);
    await pointer.drag(at, [at[0], at[1] - up], 8);
    await frames(3);
}
const boards = (col: number, row: number) => map.objects().some((o) => o.overlay === kPool && o.symbol === "DBTopBoard" && o.col === col && o.row === row);
expect(map.objects().some((o) => o.overlay === kPool && o.symbol === "DBTopBoard" && o.col === c0 + 5 && o.height === 14), "the far tower five levels high");
expect(boards(c0 + 6, r0 + 4) && boards(c0 + 4, r0 + 6) && boards(c0 + 5, r0 - 1) && boards(c0 - 1, r0 + 5), `four towers' boards over their pool, not ${JSON.stringify(pieces().filter((o) => o.symbol === "DBTopBoard"))}`);
// One laid on a water square at the pool's wall: the square turns to paving at the pool's surface, paid for less
// the water's refund, the tower stands on it, and its boards reach out over the water square in front.
camera.look({ col: c0 + 6.5, row: r0 + 2.5 }, { col: c0 + 3, row: r0 + 4.5, up: 6 });
await frames(10);
const on_square = (symbol: string, col: number, row: number) => pieces().some((o) => o.symbol === symbol && o.col === col && o.row === row);
expect(on_square("Base", c0 + 6, r0 + 2), "water at the wall before");
cash = park.cash()!;
pointer.move(...on_view(c0 + 6.5, r0 + 2.8));
await frames(3);
expect(on_square("SolidTop", c0 + 6, r0 + 2) && !on_square("Base", c0 + 6, r0 + 2) && on_square("DBBase", c0 + 6, r0 + 2), "the tower shown on paving where the water was");
await screenshot("pool_high_dive_on_water_shown");
const at_wall = on_view(c0 + 6.5, r0 + 2.8);
await pointer.drag(at_wall, [at_wall[0], at_wall[1] - 0.08], 8);
await frames(3);
expect(on_square("SolidTop", c0 + 6, r0 + 2) && !on_square("Base", c0 + 6, r0 + 2) && on_square("DBBase", c0 + 6, r0 + 2), "the water square paved under the tower");
expect(pieces().find((o) => o.symbol === "SolidTop" && o.col === c0 + 6 && o.row === r0 + 2)!.height === pieces().find((o) => o.symbol === "SolidTop" && o.col === c0 + 7 && o.row === r0 + 2)!.height, "the paving at the pool's surface");
// Facing away from the wall, straight out over the pool: its boards cover the tower's square and the squares
// either side along the columns.
expect(boards(c0 + 5, r0 + 2) && on_square("Base", c0 + 5, r0 + 2), `its boards straight out from the wall over the water, not ${JSON.stringify(pieces().filter((o) => o.symbol === "DBTopBoard"))}`);
expect(park.cash()! < cash, "the tower paid for");
await screenshot("pool_high_dive_on_water");
build.stop();
camera.look({ col: c0 + 6.5, row: r0 + 2.5 }, { col: c0 + 6.6, row: r0 + 2.5, up: 30 });
await frames(10);
await screenshot("pool_high_dive_on_water_above");
// Water again, as the pool was, for the shapes after.
expect(build.pick(kPool, "Pool_DepthNormal"), "the water in the pool list");
camera.look({ col: c0 + 6.5, row: r0 + 2.5 }, { col: c0 + 3, row: r0 + 4.5, up: 6 });
await frames(10);
await pointer.click(...on_view(c0 + 6.5, r0 + 2.8));
await frames(3);
expect(on_square("Base", c0 + 6, r0 + 2) && !on_square("DBBase", c0 + 6, r0 + 2), "water again where the tower stood");
expect(build.pick(kPool, "SegmentTypeHighDive"), "the high diving board in the pool list");
// The tool put away first, so no square is shown laid under the pointer.
build.stop();
camera.look({ col: c0 + 4, row: r0 + 4 }, { col: c0 + 4, row: r0 + 4.5, up: 30 });
await frames(10);
await screenshot("pool_high_dives_above");
// Each from straight over the water in front of it, its boards reaching out over that square, across the wall,
// to be seen alike with the saved park's tower in pool_dives.ts.
for (const [col, row, dc, dr, name] of [[c0 + 7, r0 + 4, -1, 0, "east"], [c0 + 4, r0 + 7, 0, -1, "south"], [c0 + 5, r0, 0, 1, "north"], [c0, r0 + 5, 1, 0, "west"]] as const)
{
    // Leaning a touch across the boards, not along them, so their height does not slide them on the view.
    camera.look({ col: col + 0.5 + dc, row: row + 0.5 + dr }, { col: col + 0.5 + dc + 0.1 * Math.abs(dr), row: row + 0.5 + dr + 0.1 * Math.abs(dc), up: 30 });
    await frames(10);
    await screenshot(`pool_high_dive_${name}`);
}
camera.look({ col: c0 + 1.5, row: r0 + 2.5 }, { col: c0 + 4, row: r0 + 6, up: 3 });
await frames(10);
await screenshot("pool_extras");

// A changing room on the ground beside the paved ring, opening on it: the railing there opens. Picked in the
// panel, it follows the pointer as a ghost, shown in the panel's disc too, and turns to the pool unasked; placed,
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
// The complex has a wave machine, so its settings are shown.
for (const name of ["WaveHeightText", "WaveLengthText", "WaveOnPeriodText", "WaveTotalPeriodText"])
    expect(ui.find(`info/body/${name}`) !== undefined, `the ${name} setting there, not ${JSON.stringify(ui.children("info/body"))}`);
for (const tab of ["Attraction_Finances", "Attraction_Customers", "Attraction_Maintenance"])
{
    await ui.click(`info/${tab}`);
    await frames(3);
    await screenshot(`pool_room_${tab}`);
}
await ui.click("info/close");

// With no tool out, a line drawn on the water along the grid sends waves across it, and a circle makes a
// whirlpool; the wave machine runs from the clock's start.
await frames(2);
// The machine's front, seen from the pool's side: a train of crests rolling out from its square of wall, and a
// moment later further out, dying away before the far side.
camera.look({ col: c0 + 3, row: r0 + 1.5 }, { col: c0 + 3, row: r0 + 6, up: 2.5 });
await frames(60);
await screenshot("pool_wave_machine");
await frames(25);
await screenshot("pool_wave_machine_later");
// Seen low over the water from its far corner, so the surface's rise and fall shows.
camera.look({ col: c0 + 3, row: r0 + 3 }, { col: c0 + 7.5, row: r0 + 7.5, up: 2.5 });
await frames(10);
pointer.move(...on_view(c0 + 1.5, r0 + 4.5));
pointer.press();
for (let i = 1; i <= 8; ++i)
{
    pointer.move(...on_view(c0 + 1.5 + i * 0.5, r0 + 4.5));
    await frames(1);
}
pointer.release();
await frames(40);
await screenshot("pool_wave_line");
const circle = (a: number): [number, number] => on_view(c0 + 4.5 + Math.cos(a), r0 + 4.5 + Math.sin(a)); // Near the view
pointer.move(...circle(0));
pointer.press();
for (let i = 1; i <= 24; ++i)
{
    pointer.move(...circle((i * Math.PI) / 8));
    await frames(1);
}
pointer.release();
await frames(90);
await screenshot("pool_whirlpool");

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

// A changing room on each other side of the pool, each turned unasked to the paving there, or where two sides are
// paved, the one the pointer is nearer.
const sides = [[c0 - 1, r0 + 4, 0.5, 0.5], [c0 + 8, r0 + 3, 0.2, 0.5], [c0 + 3, r0 - 1, 0.5, 0.8], [c0, r0 + 7, 0.5, 0.5]];
const rooms = () => sides.filter(([col, row]) => map.objects().some((o) => o.overlay !== kPool && o.col === col && o.row === row)).length;
camera.look({ col: c0 + 4, row: r0 + 4 }, { col: c0 + 4, row: r0 + 16, up: 14 });
await frames(10);
for (const [col, row, x, y] of sides.slice(1))
{
    await open_editor();
    await ui.click("pool_panel/Pool_ChangingRoomPanel");
    await until(() => (ui.children("pool_panel/body")?.length ?? 0) > 1, 120, "the changing rooms");
    await ui.click(`pool_panel/body/${ui.children("pool_panel/body")![0]}`);
    await frames(2);
    await pointer.click(...on_view(col + x, row + y));
    await until(() => ui.find("info") !== undefined, 120, `the window of the room at ${col - c0}, ${row - r0}`);
    await ui.click("info/close");
    await frames(3);
    expect(map.pavings().some((p) => p.col === col && p.row === row), `the floor of the room at ${col - c0}, ${row - r0} paved`);
}
expect(rooms() === 4, `a changing room on each side, not ${rooms()}`);
await screenshot("pool_rooms_each_side");

// The changing room's window again, its complex's name kept; selling the complex takes up its squares, extras
// and rooms, once asked, and leaves the pool below.
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
expect(first() === 0 && rooms() === 0, `the complex gone, not ${first()} pieces and rooms at ${JSON.stringify(sides.filter(([col, row]) => map.objects().some((o) => o.overlay !== kPool && o.col === col && o.row === row)).map(([col, row]) => [col - c0, row - r0]))}`);
expect(pieces().length === below, "the pool below kept");
expect(park.cash()! > cash, "money back for the complex");

// Where the complex stood, paved squares raised three units off the ground, the land under them raised to their
// floor, and a spiral staircase climbing to them from a paved square on the ground beside.
const level = Math.round(map.tile(c0 - 1, r0 - 1)!.corners[0]);
camera.look({ col: c0 + 3, row: r0 + 2.5 }, { col: c0 + 7, row: r0 + 9, up: 7 });
await frames(10);
await open_editor();
await ui.click("pool_panel/body/editor/Pool_PlaceTiles");
await pointer.drag(on_view(c0 + 4.5, r0 + 2.5), on_view(c0 + 5.5, r0 + 2.5), 6);
await frames(3);
await ui.click("pool_panel/body/editor/Pool_Height");
await ui.click("pool_panel/body/editor/height/field");
keys.press("Ctrl+A");
keys.type(`${level + 3}`);
keys.press("Enter");
await frames(2);
cash = park.cash()!;
await pointer.drag(on_view(c0 + 1.5, r0 + 1.5), on_view(c0 + 3.5, r0 + 3.5), 6);
await frames(3);
const raised = pieces().filter((o) => o.symbol === "SolidTop" && o.row < r0 + 8 && o.height === level + 3).length;
expect(raised === 9 && park.cash()! < cash, `nine raised squares, paid for, not ${raised}`);
expect(map.tile(c0 + 2, r0 + 2)!.corners.every((h) => Math.abs(h - (level + 1)) < 0.01), `the land raised to their floor, not ${map.tile(c0 + 2, r0 + 2)!.corners}`);
await ui.click("pool_panel/body/editor/Pool_Height");
await ui.click("pool_panel/Pool_PathPanel");
await until(() => ui.find("pool_panel/body/Icon_Default_SpiralStairs") !== undefined, 120, "the spiral staircase on the paths page");
expect(ui.children("pool_panel/body")!.filter((n) => n !== "paths").length === 3, `two ramps and the staircase on the paths page, not ${JSON.stringify(ui.children("pool_panel/body"))}`);
// Each way between levels is named, with its price, as the pointer rests on it: the staircase its foot's.
const stairs_help = ui.find("pool_panel/body/Icon_Default_SpiralStairs")!;
expect(stairs_help.tooltip === "Spiral Stairs : $24.00", `the staircase's help, not ${stairs_help.tooltip}`);
const ramp_help = ui.children("pool_panel/body")!.filter((n) => n !== "paths" && n !== "Icon_Default_SpiralStairs").map((n) => ui.find(`pool_panel/body/${n}`)!.tooltip);
expect(ramp_help.length === 2 && ramp_help.every((h) => h?.match(/^\dm Ramp : \$12\.00$/)), `the ramps' help, not ${JSON.stringify(ramp_help)}`);
pointer.move(stairs_help.x, stairs_help.y);
await frames(60);
await screenshot("pool_panel_tooltip");
await ui.click("pool_panel/body/Icon_Default_SpiralStairs");
await frames(2);
cash = park.cash()!;
await pointer.click(...on_view(c0 + 4.5, r0 + 2.5));
await frames(3);
// Laid on the paving at its lowest, it costs its price: its climb, less the paving it takes the place of.
const stairs_paid = cash - park.cash()!;
expect(count("StairsBase") === 1 && count("StairsTop") === 1 && stairs_paid === 2400, `a staircase, paid $24.00, not ${count("StairsBase")} for ${stairs_paid}`);
// A ramp from the same column, against a side of the paved square past the staircase.
const ramp = ui.children("pool_panel/body")!.find((n) => n !== "paths" && n !== "Icon_Default_SpiralStairs")!;
await ui.click(`pool_panel/body/${ramp}`);
await frames(2);
const before_ramp = pieces().length;
cash = park.cash()!;
await pointer.click(...on_view(c0 + 5.5, r0 + 2.5));
await frames(3);
expect(pieces().length > before_ramp && park.cash()! < cash, `a ramp, paid for, not ${pieces().length - before_ramp} pieces more`);
build.stop();
await frames(5);
await screenshot("pool_raised_stairs");

// Pool paths from the paths tab: an L of path beside the raised squares, then the path panel's controls going on
// from its end: a slope up, a flat piece after it, and that piece taken away again.
camera.look({ col: c0 + 6, row: r0 + 4 }, { col: c0 + 6, row: r0 + 13, up: 9 });
await frames(10);
cash = park.cash()!;
await open_editor();
await ui.click("pool_panel/Pool_PathPanel");
await until(() => ui.children("pool_panel/body")?.includes("paths") === true, 120, "the paths page");
expect(ui.children("pool_panel/body/paths")!.length === 9, `the path controls, not ${JSON.stringify(ui.children("pool_panel/body/paths"))}`);
await screenshot("pool_panel_paths");
await pointer.drag(on_view(c0 + 6.5, r0 + 2.5), on_view(c0 + 8.5, r0 + 5.5), 6);
await frames(3);
expect(count("PathCorner") === 1 && count("PathStraight") === 5 && park.cash()! < cash, `an L of path, paid for, not ${count("PathCorner")} corners and ${count("PathStraight")} straights`);
await ui.click("pool_panel/body/paths/Path_UpwardSlope");
await ui.click("pool_panel/body/paths/Path_Straight");
await frames(3);
const slope = pieces().find((o) => o.symbol === "PathSlope");
expect(slope !== undefined && slope.col === c0 + 8 && slope.row === r0 + 6, `a slope on from the L's end, not ${JSON.stringify(slope)}`);
await ui.click("pool_panel/body/paths/Path_NoSlope");
await ui.click("pool_panel/body/paths/Path_Straight");
await frames(3);
const above = pieces().find((o) => o.symbol.startsWith("Path") && o.col === c0 + 8 && o.row === r0 + 7);
expect(above !== undefined && above.height! > slope!.height!, `a path past the slope's top, not ${JSON.stringify(above)}`);
await screenshot("pool_paths_trail");
await ui.click("pool_panel/body/paths/PathEditor_Delete");
await frames(3);
expect(!pieces().some((o) => o.col === c0 + 8 && o.row === r0 + 7), "the last piece taken away");
build.stop();
await frames(5);
await screenshot("pool_paths");

// Below the raised squares, two paved squares raised with Shift: held over the first, the pointer moved up the
// screen lifts its surface; the drag then lays both at that height. Let go of, the surface follows the ground again.
await open_editor();
await ui.click("pool_panel/body/editor/Pool_PlaceTiles");
await frames(2);
const lift = on_view(c0 + 2.5, r0 + 6.5);
pointer.move(...lift);
await frames(3);
pointer.shift(true);
for (let i = 1; i <= 6; ++i)
{
    pointer.move(lift[0], lift[1] - i * 0.015);
    await frames(1);
}
await frames(3);
pointer.press();
await frames(2);
pointer.move(...on_view(c0 + 3.5, r0 + 6.5));
await frames(3);
pointer.release();
pointer.shift(false);
await frames(3);
const lifted = pieces().filter((o) => o.symbol === "SolidTop" && o.row === r0 + 6);
expect(lifted.length === 2 && lifted.every((o) => o.height !== null && o.height >= level + 2), `two squares raised with Shift, not ${JSON.stringify(lifted)} over ${level}`);
build.stop();
camera.look({ col: c0 + 3, row: r0 + 6.5 }, { col: c0 + 3, row: r0 + 11, up: 4 });
await frames(10);
await screenshot("pool_shift_raised");
