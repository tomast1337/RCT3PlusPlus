// SPDX-License-Identifier: GPL-3.0-or-later
//
// Guests swim. On the game's blank landscape a pool goes in beside the road, paved round with water in the
// middle, and a changing room between the road and the pool, opened. On a warm day guests come in, a good share of
// them to swim: those wishing for a swim pay at the room, change out of sight and swim, in their swimwear, and lie
// on the sun loungers and stand under the shower along the far side.
//
//   ./nob script scripts/tests/pool_guests.ts -- --no-intro --no-audio --seed 1

export {};

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
const rides = `tools/Rides/${must(ui.children("tools/Rides"), "tools/Rides shown")[0]}`;
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

// Sun loungers and a shower on the paving along the far side; swimmers lie on the one and stand under the other.
for (const [symbol, col] of [["SunLoungers", c + 4], ["PoolShower", c + 6]] as const) {
    expect(build.pick(kPool, symbol), `${symbol} in the pool extras`);
    await frames(3);
    await pointer.click(...on_view(col + 0.5, r + 3.5));
    await frames(3);
    expect(map.objects().some((o) => o.symbol === symbol && o.col === col && o.row === r + 3), `${symbol} on the paving`);
}

// The changing room between the road and the pool, turned to open on it unasked; placed, its window opens, where the
// complex opens.
await until(() => ui.find("pool_panel/Pool_ChangingRoomPanel") !== undefined, 120, "the pool panel");
await ui.click("pool_panel/Pool_ChangingRoomPanel");
await frames(2);
await ui.click(`pool_panel/body/${must(ui.children("pool_panel/body"), "pool_panel/body shown")[0]}`);
await frames(3);
await pointer.click(...on_view(c + 1.5, r + 0.5));
await frames(3);
expect(map.objects().some((o) => o.overlay !== kPool && o.col === c + 1 && o.row === r), "the changing room beside the road");
// The pool panel stays out once the room is placed; closed, a click on the room opens its window.
await ui.click("pool_panel/close");
await frames(3);
await pointer.click(...on_view(c + 1.5, r + 0.5));
await until(() => ui.find("info") !== undefined, 120, "the changing room's window");
await ui.click("info/Attraction_General_Closed");
await until(() => ui.find("info/Attraction_Button_Open") !== undefined, 60, "the flag's lights");
await ui.click("info/Attraction_Button_Open");
await frames(3);
expect(ui.find("info/Attraction_General_Open") !== undefined, `the complex open, not ${JSON.stringify(ui.children("info"))}`);
await screenshot("pool_guests_open");
await ui.click("info/close");

// A warm, sunny day, and guests.
park.setWeather("sunny");
park.setTemperature(28);
const cash = park.cash();
const dropped = park.dropParties(40, 7).reduce((n, p) => n + p.guests, 0);
log("dropped", dropped, "guests");
// Seen in the water, after changing.
const in_pool = () => swimmers().filter((p) => p.col !== undefined && p.col > c + 3.2 && p.col < c + 6.8 && p.row! > r + 0.2 && p.row! < r + 2.8);
await until(() => in_pool().length >= 4, 6000, `guests in the pool (${JSON.stringify(swimmers())})`);
log("swimming", swimmers().length, "in sight", in_pool().length, JSON.stringify(swimmers().slice(0, 6)), "pool at", c + 3, r);
// On a warm day a good share of the guests swim, as many as half the room twelve squares of water make, at the least.
const guests = () => people.list().filter((p) => !p.staff && !p.leaving).length;
await until(() => swimmers().length >= Math.min(0.15 * guests(), 12), 3000, `a share of the guests swimming (${swimmers().length} of ${guests()})`);
log("share swimming", swimmers().length, "of", guests());
log("cash", cash, "then", park.cash());
if (cash !== undefined)
    await until(() => park.cash()! > cash, 600, "the tickets paid");
camera.look({ col: c + 5, row: r + 1.5 }, { col: c + 5, row: r + 5, up: 2.5 });
await frames(300);
await screenshot("pool_guests_swimming");
camera.look({ col: c + 5, row: r + 3.5 }, { col: c + 5, row: r + 5.5, up: 1.5 });
await frames(1800);
await screenshot("pool_guests_extras");
// Close up, lying on the lounger.
await until(() => people.list().some((p) => p.pool_extra === "lounger"), 3000, "a swimmer on the lounger");
camera.look({ col: c + 4.5, row: r + 3.5 }, { col: c + 6, row: r + 5, up: 1.2 });
await frames(10);
await screenshot("pool_guests_lounger");

// The room's window counts the swimmers, and its upkeep shows the water's dirt; the harshest fluid is picked.
camera.look({ col: c + 3, row: r + 0.5 }, { col: c - 1, row: r + 3, up: 5 });
await frames(10);
// On its roof, above the people at its door.
const [room_x, room_y] = on_view(c + 1.5, r + 0.5);
await pointer.click(room_x, room_y - 0.08);
await until(() => ui.find("info") !== undefined, 120, "the changing room's window");
await ui.click("info/Guest_Rides");
await frames(3);
const inside = ui.find("info/body/status_label")?.text ?? "";
expect(/^([5-9]|\d\d+) people/.test(inside), `the swimmers counted, not ${inside}`);
await screenshot("pool_guests_customers");
await ui.click("info/Attraction_Maintenance");
await frames(3);
expect(ui.find("info/body/PoolDirt_Label_label") !== undefined, `the water's dirt shown, not ${JSON.stringify(ui.children("info/body"))}`);
const fluid = must(ui.find("info/body/Fluid_TypeLabel"), "info/body/Fluid_TypeLabel shown");
await pointer.drag([fluid.x, fluid.y], [fluid.x, fluid.bottom + (fluid.bottom - fluid.top) * 3.5], 6);
await frames(3);
await screenshot("pool_guests_upkeep");
await ui.click("info/close");
camera.look({ col: c + 3, row: r + 0.5 }, { col: c - 1, row: r + 3, up: 5 });
await frames(30);
await screenshot("pool_guests_room");
