// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sellers come with what they sell at, as in the game. On the game's blank landscape a stall and a flat ride
// with its entrance and exit are placed: a seller stands at each, in the stall and in the entrance's booth.
// The entrance moved, its seller moves with it. Saved and loaded again, both are still there, and once the
// stall is pulled down its seller is gone.
//
//   RCT3PP_DATA=<an empty folder> ./nob script scripts/tests/vendors.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

/** The sellers in the park, each with the object they serve at. */
const sellers = () => people.list().filter((p) => p.post !== undefined);

/** The seller standing on or beside tile `spot`, if any. */
const seller_at = (spot: Spot | null | undefined) => sellers().find((p) => spot && p.at && Math.abs(p.at.col - (spot.col + 0.5)) < 1.5 && Math.abs(p.at.row - (spot.row + 0.5)) < 1.5);

/** Waits a few of the park's people refreshes for the sellers to be `count`. */
async function sellers_come(count: number, what: string): Promise<void> {
    await until(() => sellers().length === count, 120, `${count} sellers ${what} (${JSON.stringify(sellers())})`);
}

/** Clicks a tool's own button, which comes first in its group. */
async function click_tool(tool: string): Promise<void> {
    await ui.click(`tools/${tool}/${ui.children(`tools/${tool}`)![0]}`);
}

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.stalls() !== undefined && build.flatRides() !== undefined, 1200, "the build lists");
expect(sellers().length === 0, "no sellers in the blank park");
const gate = map.paths().reduce((a, b) => (b.row > a.row ? b : a));
const road = { col: gate.col, row: gate.row - 4 };

/** Clicks tile `col`, `row` while placing step `step`. */
async function place(step: string, col: number, row: number): Promise<void> {
    const at = map.screen(col, row);
    expect(at !== undefined, `tile ${col},${row} in sight`);
    pointer.move(at!.x, at!.y);
    await frames(3);
    const placing = build.placing();
    expect(placing?.step === step && placing.problem === "none", `${step} placeable at ${col},${row} (${JSON.stringify(placing)})`);
    await pointer.click(at!.x, at!.y);
    await frames(3);
}

// A stall beside the road, facing it.
const kind = build.stalls()!.find((s) => s.overlay === "Style/Vanilla/Style" && s.symbol.toLowerCase().includes("burger"))!;
expect(kind !== undefined, "the burger stall");
camera.look({ col: road.col + 0.5, row: road.row + 0.5 }, { col: road.col + 0.5, row: road.row + 10.5, up: 25 });
await frames(3);
expect(build.pick(kind.overlay, kind.symbol), `${kind.symbol} picked`);
await frames(2);
await ui.click("placing/IconAttractionEditorRotate");
await place("ride", road.col + 1, road.row);
const stall = stalls.list().findIndex((s) => s.symbol === `${kind.symbol}:sta`);
expect(stall >= 0, "the stall placed");
await sellers_come(1, "once the stall stands");
expect(seller_at(stalls.list()[stall].site) !== undefined, `the seller in the stall (${JSON.stringify(sellers())}, stall ${JSON.stringify(stalls.list()[stall].site)})`);
await ui.click("info/close");
// From the road it serves.
camera.look({ col: road.col + 1.5, row: road.row + 0.5 }, { col: road.col - 2.5, row: road.row + 1.5, up: 3 });
await frames(30);
await screenshot("vendor_stall");

// A flat ride east of it, its entrance in front of its first column and its exit by its last row.
const ride_kind = build.flatRides()!.filter((r) => r.overlay.startsWith("Style/Vanilla/") && r.doors && !r.water && r.width >= 2 && r.width <= 4 && r.length <= 4).sort((a, b) => a.width * a.length - b.width * b.length)[0];
expect(ride_kind !== undefined, "a small flat ride with doors");
const first = road.col + 4;
const last = road.row - 2;
const right = first + ride_kind.width - 1;
camera.look({ col: first + 0.5, row: last + 0.5 }, { col: first + 0.5, row: last + 14.5, up: 35 });
await frames(3);
expect(build.pick(ride_kind.overlay, ride_kind.symbol), `${ride_kind.symbol} picked`);
await frames(2);
await place("ride", first + ((ride_kind.width - 1) >> 1), last - ((ride_kind.length - 1) >> 1));
await place("entrance", first, last + 1);
await place("exit", right + 1, last);
const ride = rides.list().findIndex((r) => r.symbol === `${ride_kind.symbol}:anr`);
expect(ride >= 0 && rides.list()[ride].entrance !== null, `the ride and its entrance placed (${JSON.stringify(rides.list()[ride])})`);
await sellers_come(2, "once the entrance stands");
expect(seller_at(rides.list()[ride].entrance) !== undefined, `a seller in the entrance (${JSON.stringify(sellers())}, entrance ${JSON.stringify(rides.list()[ride].entrance)})`);
if (ui.find("info") !== undefined)
    await ui.click("info/close");
// Close, at its window.
camera.look({ col: first + 0.5, row: last + 1.5, up: 1 }, { col: first - 1.5, row: last + 1.5, up: 1.5 });
await frames(30);
await screenshot("vendor_entrance");

// The entrance moved one along: its seller goes with it, a new one at the new entrance.
const old_post = seller_at(rides.list()[ride].entrance)!.post;
rides.show(ride);
await frames(5);
camera.look({ col: first + 0.5, row: last + 0.5 }, { col: first + 0.5, row: last + 14.5, up: 35 });
await ui.click("info/body/Attraction_General_Entrance");
await frames(2);
await place("entrance", first + 1, last + 1);
build.stop();
await frames(2);
expect(rides.list()[ride].entrance?.col === first + 1, `the entrance moved (${JSON.stringify(rides.list()[ride].entrance)})`);
await until(() => sellers().length === 2 && sellers().every((p) => p.post !== old_post), 120, `the old entrance's seller gone, a new one come (${JSON.stringify(sellers())})`);
expect(seller_at(rides.list()[ride].entrance) !== undefined, `the new seller at the new entrance (${JSON.stringify(sellers())})`);
const posts = sellers().map((p) => p.post).sort();

// Saved and loaded again, both are there, at the same posts.
if (ui.find("info") !== undefined)
    await ui.click("info/close");
await click_tool("Files");
await ui.click("tools/Files/File_SaveGame");
await until(() => ui.find("park_files") !== undefined, 120, "the save window");
await ui.click("park_files/content/open");
await until(() => ui.find("park_files") === undefined, 120, "the save window to close");
if (!ui.find("tools/Files/File_LoadGame"))
    await click_tool("Files");
await until(() => ui.find("tools/Files/File_LoadGame") !== undefined, 120, "the files menu");
await ui.click("tools/Files/File_LoadGame");
const row0 = "park_files/content/list/view/content/row0";
await until(() => ui.find(row0) !== undefined, 120, "the save in the load window");
await ui.click(row0);
await ui.click("park_files/content/open");
await frames(30);
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.stalls() !== undefined, 1200, "the saved park");
await frames(60);
expect(JSON.stringify(sellers().map((p) => p.post).sort()) === JSON.stringify(posts), `the same sellers after loading (${JSON.stringify(posts)} then ${JSON.stringify(sellers())})`);
const again = stalls.list().findIndex((s) => s.symbol === `${kind.symbol}:sta`);
expect(seller_at(stalls.list()[again].site) !== undefined, "the stall's seller loaded in it");
camera.look({ col: road.col + 2.5, row: road.row + 0.5 }, { col: road.col + 2.5, row: road.row + 6.5, up: 6 });
await frames(30);
await screenshot("vendor_loaded");

// The stall pulled down takes its seller with it.
stalls.show(again);
await frames(10);
await ui.click("info/body/Attraction_General_Delete");
await frames(2);
await ui.click("demolish_question/content/GUI2MiniDialog_Confirm");
await frames(2);
expect(stalls.list()[again].demolished, "the stall pulled down");
await sellers_come(1, "with the stall gone");
expect(seller_at(stalls.list()[again].site) === undefined, "no seller left where the stall stood");
quit(0);
