// SPDX-License-Identifier: GPL-3.0-or-later
//
// A pool built on the blank landscape fills with swimmers on the day the park starts, a cool spring day,
// with the weather left as it is: paved round with water in the middle, a changing room between the road and
// the pool, opened from its window. Guests walk in at the room's door, change and swim; fewer than on a hot day,
// and some say it is too cold.
//
//   ./nob script scripts/tests/pool_entry.ts -- --no-intro --no-audio --seed 1

export {};

const kPool = "Pool/Default/Default_Stub";

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(
    () => ui.find("tools/Rides/Obj_Pools") !== undefined || ui.find("tools/Rides") !== undefined,
    600,
    "the tool row",
);
await until(() => build.flatRides() !== undefined, 1200, "the build items");
const gate = map.paths().reduce((a, b) => (b.row > a.row ? b : a));
const c = gate.col;
const r = gate.row - 4;
camera.look({ col: c + 3, row: r + 0.5 }, { col: c + 3, row: r + 12, up: 14 });
await frames(10);

// Tiles four by four east of the room's tile, water two by two inside them.
const rides = `tools/Rides/${must(ui.children("tools/Rides"), "tools/Rides shown")[0]}`;
if (!ui.find("tools/Rides/Obj_Pools")) await ui.click(rides);
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

// The changing room between the road and the pool, opened from the window that opens once it is placed.
await until(() => ui.find("pool_panel/Pool_ChangingRoomPanel") !== undefined, 120, "the pool panel");
await ui.click("pool_panel/Pool_ChangingRoomPanel");
await frames(2);
await ui.click(`pool_panel/body/${must(ui.children("pool_panel/body"), "pool_panel/body shown")[0]}`);
await frames(3);
await pointer.click(...on_view(c + 1.5, r + 0.5));
await frames(3);
expect(
    map.objects().some((o) => o.overlay !== kPool && o.col === c + 1 && o.row === r),
    "the changing room beside the road",
);
// The pool panel stays out once the room is placed; closed, a click on the room opens its window.
await ui.click("pool_panel/close");
await frames(3);
await pointer.click(...on_view(c + 1.5, r + 0.5));
await until(() => ui.find("info") !== undefined, 120, "the changing room's window");
await ui.click("info/Attraction_General_Closed");
await until(() => ui.find("info/Attraction_Button_Open") !== undefined, 60, "the flag's lights");
await ui.click("info/Attraction_Button_Open");
await frames(3);
expect(ui.find("info/Attraction_General_Open") !== undefined, "the complex open");
await ui.click("info/close");

// The weather as the park starts: under the warmth of a summer's day.
const look = park.weatherLook();
log("weather", park.weather(), "temperature", look.temperature);
expect(look.temperature < 20, `a cool day, not ${look.temperature} degrees`);
const dropped = park.dropParties(40, 7).reduce((n, p) => n + p.guests, 0);
log("dropped", dropped, "guests");

// Through the room's door: changing (in their clothes, out of sight) or out on the deck and in the water.
const at_pool = () => people.list().filter((p) => p.state === "swimming");
const in_pool = () =>
    at_pool().filter(
        (p) =>
            p.swimwear &&
            p.col !== undefined &&
            p.col > c + 3.2 &&
            p.col < c + 6.8 &&
            p.row! > r + 0.2 &&
            p.row! < r + 2.8,
    );
const cold = () => people.list().filter((p) => p.thoughts?.includes(198)).length;
camera.look({ col: c + 1.5, row: r + 0.5 }, { col: c - 2, row: r + 3, up: 3 });
await until(() => at_pool().length >= 1, 6000, "a guest going in at the changing room");
await frames(5);
await screenshot("pool_entry_walking_in");
await until(() => in_pool().length >= 3, 6000, `guests in the pool (${at_pool().length} at it, ${cold()} too cold)`);
log(
    "at the pool",
    at_pool().length,
    "in the water",
    in_pool().length,
    "too cold",
    cold(),
    "temperature",
    park.weatherLook().temperature,
);
camera.look({ col: c + 5, row: r + 1.5 }, { col: c + 5, row: r + 5, up: 2.5 });
await frames(60);
await screenshot("pool_entry_swimming");
