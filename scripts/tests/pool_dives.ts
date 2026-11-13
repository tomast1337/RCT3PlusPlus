// SPDX-License-Identifier: GPL-3.0-or-later
//
// Diving. In a park the game saved with a high diving board in a pool and guests swimming, swimmers climb its
// ladder, walk out on a board, dive off its end and splash into the water, one at a time. A small diving board put
// on the pool's wall is walked out on and dived off too. Give it such a park:
//
//   ./nob script scripts/tests/pool_dives.ts -- --no-intro --no-audio --seed 1 --park PARK

export {};

const kPool = "Pool/Default/Default_Stub";

await until(() => park.loaded(), 1200, "the park");
await frames(10);
park.setWeather("sunny");
park.setTemperature(28);
park.setTime(0.5);
// Free look, so the camera can stand level with someone up a board rather than over the ground.
options.set("camera_mode", 1);
const tower = map.objects().find((o) => o.overlay === kPool && o.symbol === "DBTopBoard");
expect(tower !== undefined, "a high diving board in the park");
// The tower as the park has it, from both sides along its boards.
const base = must(map.objects().find((o) => o.overlay === kPool && o.symbol === "DBBase"), "the diving board's base");
for (const [name, dc, dr] of [["pool_dives_tower", 6, 4], ["pool_dives_tower_back", -5, -4]] as const)
{
    camera.look({ col: base.col + 0.5, row: base.row + 0.5, up: 8 }, { col: base.col + 0.5 + dc, row: base.row + 0.5 + dr, up: 8 });
    await frames(5);
    await screenshot(name);
}
// And from straight over the water square in front of it, as pools.ts shows the towers it lays.
camera.look({ col: base.col + 1.5, row: base.row + 0.5 }, { col: base.col + 1.5, row: base.row + 0.6, up: 30 });
await frames(5);
await screenshot("pool_dives_tower_above");

const diver = (high: boolean, step?: string) => people.list().findIndex((p) => p.diving !== undefined && p.high_board === high && (step === undefined || p.diving === step));
const look_at = async (i: number, name: string, behind = false) => {
    // Paused, so they stay where they are for the picture.
    await ui.click("bar/Top_Pause");
    const p = people.list()[i];
    log(name, p.diving, "at", p.col?.toFixed(2), p.row?.toFixed(2), "up", p.up?.toFixed(2));
    // Close and level with them, from the side of the board (both boards here reach out along the columns) or
    // from behind the tower. A person's `up` is their height in the world, a map point's over the ground there.
    const ground = (col: number, row: number) => (camera.look({ col, row }), camera.view().target[1]);
    const from = behind ? { col: p.col! - 2, row: p.row! } : { col: p.col!, row: p.row! + 2 };
    camera.look({ col: p.col!, row: p.row!, up: p.up! + 0.9 - ground(p.col!, p.row!) }, { ...from, up: p.up! + 1.2 - ground(from.col, from.row) });
    await frames(3);
    await screenshot(name);
    await ui.click("bar/Top_Pause");
};

// Up the ladder, out on a board, off its end and into the water; nobody else on it meanwhile.
await until(() => diver(true, "climb") >= 0, 6000, "a swimmer climbing the high board");
const high = diver(true);
// Where they set off from, at the ladder's foot: the ground the pool is on need not be at height 0.
const start = people.list()[high];
const foot = start.up!;
await look_at(high, "pool_dives_climb");
await look_at(high, "pool_dives_climb_back", true);
let most = 0;
const count = () => (most = Math.max(most, people.list().filter((p) => p.diving !== undefined && p.high_board).length));
await until(() => (count(), people.list()[high].diving === "walk"), 3600, "the diver up on a board");
const top = people.list()[high].up!;
await until(() => (count(), people.list()[high].diving === "jump"), 600, "the diver at the board's end");
await look_at(high, "pool_dives_board");
await until(() => (count(), people.list()[high].diving === "fall" && people.list()[high].up! < (top + foot) / 2), 600, "the diver halfway down");
await look_at(high, "pool_dives_fall");
await until(() => (count(), people.list()[high].diving === "surface"), 600, "the diver in the water");
await frames(2);
await screenshot("pool_dives_splash");
await until(() => (count(), people.list()[high].diving === undefined), 600, "the diver swimming on");
expect(people.list()[high].state === "swimming" && people.list()[high].swimwear === true, "the diver swimming on in their swimwear");
expect(most === 1, `one on the high board at a time, not ${most}`);

// A small board on the water's edge beside the high one they dived off, turned until it fits against the wall.
const off = map.objects().filter((o) => o.overlay === kPool && o.symbol === "DBTopBoard").sort((a, b) => Math.hypot(a.col - start.col!, a.row - start.row!) - Math.hypot(b.col - start.col!, b.row - start.row!))[0];
const extras = () => map.objects().filter((o) => o.overlay === kPool && !/^(Curve)?(Solid|Edge|Corner|End|Full|Base|Railing|DB\w|Path)/.test(o.symbol));
const before = extras().length;
const boards = () => extras().length - before;
const rides = `tools/Rides/${must(ui.children("tools/Rides"), "tools/Rides shown")[0]}`;
if (!ui.find("tools/Rides/Obj_Pools"))
    await ui.click(rides);
await until(() => ui.find("tools/Rides/Obj_Pools") !== undefined, 120, "the rides menu");
await ui.click("tools/Rides/Obj_Pools");
await until(() => ui.find("pool_panel") !== undefined, 120, "the pool panel");
// The panel opens on its water tool; pick the board once it has.
await frames(5);
expect(build.pick(kPool, "DB"), "the diving board in the pool list");
const water = map.objects().filter((o) => o.overlay === kPool && /^(Curve)?Edge\d*Top$/.test(o.symbol) && (o.col !== off.col || o.row !== off.row));
const away = (o: { col: number; row: number }) => Math.abs(o.col - off.col) + Math.abs(o.row - off.row);
water.sort((a, b) => away(a) - away(b));
expect(water.length > 0, "the pool's wall near the high board");
for (const square of water.slice(0, 30))
{
    // From above, so the tower hides no square.
    camera.look({ col: square.col + 0.5, row: square.row + 0.5 }, { col: square.col + 0.5, row: square.row + 2, up: 12 });
    await frames(5);
    for (let turn = 0; turn < 4 && boards() === 0; ++turn)
    {
        await pointer.click(...on_view(square.col + 0.5, square.row + 0.5));
        await frames(3);
        if (boards() === 0)
        {
            keys.press(",");
            await frames(2);
        }
    }
    if (boards() > 0)
        break;
}
await screenshot("pool_dives_small_placed");
log("placed", JSON.stringify(extras().slice(before)), "of", water.length);
expect(boards() === 1, "a small diving board on the pool's wall");
build.stop();
await until(() => diver(false, "jump") >= 0, 12000, "a swimmer at the small board's end");
const small = diver(false);
await look_at(small, "pool_dives_small_board");
await until(() => people.list()[small].diving === "surface", 600, "the small board's diver in the water");
await frames(2);
await screenshot("pool_dives_small_splash");
await until(() => people.list()[small].diving === undefined, 600, "the small board's diver swimming on");
log("high diver", people.list()[high].name, "small", people.list()[small].name);
