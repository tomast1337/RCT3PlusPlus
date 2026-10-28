// SPDX-License-Identifier: GPL-3.0-or-later
//
// Guests swim. On the game's blank landscape a pool goes in beside the road, paved round with water in the
// middle, and a changing room between the road and the pool, opened. On a warm day guests come in: those
// wishing for a swim pay at the room, change out of sight and swim, in their swimwear.
//
//   ./nob script scripts/tests/pool_guests.ts -- --no-intro --no-audio --seed 1

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

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Rides/Obj_Pools") !== undefined || ui.find("tools/Rides") !== undefined, 600, "the tool row");
await until(() => build.flatRides() !== undefined, 1200, "the build items");
const gate = map.paths().reduce((a, b) => (b.row > a.row ? b : a));
// The road's tile beside the room; the pool east of it, the room between.
const c = gate.col;
const r = gate.row - 4;
camera.look({ col: c + 3, row: r + 0.5 }, { col: c + 3, row: r + 12, up: 14 });
await frames(10);

// Tiles four by four east of the room's tile, water two by two inside them.
const rides = `tools/Rides/${ui.children("tools/Rides")![0]}`;
if (!ui.find("tools/Rides/Obj_Pools"))
    await ui.click(rides);
await until(() => ui.find("tools/Rides/Obj_Pools") !== undefined, 120, "the rides menu");
await ui.click("tools/Rides/Obj_Pools");
await until(() => ui.find("pool_panel/body/editor/Pool_PlaceTiles") !== undefined, 120, "the pool panel");
await ui.click("pool_panel/body/editor/Pool_PlaceTiles");
await frames(2);
await pointer.drag(on_view(c + 2.5, r - 0.5), on_view(c + 7.5, r + 3.5), 8);
await frames(3);
expect(build.pick(kPool, "Pool_DepthNormal"), "the water in the pool list");
await pointer.drag(on_view(c + 3.5, r + 0.5), on_view(c + 6.5, r + 2.5), 6);
await frames(3);
const pieces = () => map.objects().filter((o) => o.overlay === kPool);
expect(pieces().filter((o) => o.symbol === "Base").length === 12, `twelve squares of water, not ${pieces().filter((o) => o.symbol === "Base").length}`);

// The changing room between the road and the pool, turned to open on it; placed, its window opens, where the
// complex opens.
await until(() => ui.find("pool_panel/Pool_ChangingRoomPanel") !== undefined, 120, "the pool panel");
await ui.click("pool_panel/Pool_ChangingRoomPanel");
await frames(2);
await ui.click(`pool_panel/body/${ui.children("pool_panel/body")![0]}`);
keys.press(".");
await frames(3);
await pointer.click(...on_view(c + 1.5, r + 0.5));
await frames(3);
expect(map.objects().some((o) => o.overlay !== kPool && o.col === c + 1 && o.row === r), "the changing room beside the road");
await until(() => ui.find("info") !== undefined, 120, "the changing room's window");
await ui.click("info/Attraction_OpMode");
await frames(3);
const choice = ui.find("info/body/AttractionStatusOpen")!;
await pointer.drag([choice.x, choice.y], [choice.x, choice.bottom + (choice.bottom - choice.top) / 2], 6);
await frames(3);
await ui.click("info/close");

// A warm, sunny day, and guests.
park.setWeather("sunny");
park.setTemperature(28);
const cash = park.cash();
const dropped = park.dropParties(40, 7).reduce((n, p) => n + p.guests, 0);
log("dropped", dropped, "guests");
const swimmers = () => people.list().filter((p) => p.state === "swimming");
// Seen in the water, after changing.
const in_pool = () => swimmers().filter((p) => p.col !== undefined && p.col > c + 3.2 && p.col < c + 6.8 && p.row! > r + 0.2 && p.row! < r + 2.8);
await until(() => in_pool().length >= 4, 6000, `guests in the pool (${JSON.stringify(swimmers())})`);
log("swimming", swimmers().length, "in sight", in_pool().length, JSON.stringify(swimmers().slice(0, 6)), "pool at", c + 3, r);
log("cash", cash, "then", park.cash());
if (cash !== undefined)
    await until(() => park.cash()! > cash, 600, "the tickets paid");
camera.look({ col: c + 5, row: r + 1.5 }, { col: c + 5, row: r + 5, up: 2.5 });
await frames(300);
await screenshot("pool_guests_swimming");
camera.look({ col: c + 3, row: r + 0.5 }, { col: c - 1, row: r + 3, up: 5 });
await frames(30);
await screenshot("pool_guests_room");
