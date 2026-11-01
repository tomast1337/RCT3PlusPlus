// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park inspector goes round the park's attractions. On the game's blank landscape, whose inspector walks the
// road, a small flat ride and a burger stall are built by the road and opened, the stall's burger priced far above
// what guests pay for one. Then:
//
//   - the inspector queues for the ride and rides it;
//   - the inspector tells the player the stall's items are too expensive, and goes and stands at its counter.
//
//   ./nob script scripts/tests/inspector.ts -- --no-audio
//
//          ride  X b       X exit, b path behind it
//          E       b       E entrance
//          |       |
//   =====================  road
//            S             S stall, facing the road

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined && build.stalls() !== undefined, 1200, "the tool row and the build lists");
const gate = map.paths().reduce((a, b) => (b.row > a.row ? b : a));
const inspector = () => people.list().find((p) => p.role === "inspector");
await until(() => inspector() !== undefined, 120, "the park's inspector");

const item = build
    .flatRides()!
    .filter((i) => !i.water && i.doors && i.length >= 3)
    .sort((a, b) => a.width * a.length - b.width * b.length)[0];
expect(item !== undefined, "a small flat ride to build");
const col = gate.col + 4;
const first = gate.row + 4;
const last = first + item.length - 1;
const right = col + item.width - 1;
const road = last + 4;

async function look(c: number, r: number, up = 30): Promise<void> {
    camera.look({ col: c + 0.5, row: r + 0.5 }, { col: c + 0.5, row: r + 0.5 + up / 3, up });
    await frames(3);
}
function onView(c: number, r: number): [number, number] {
    const at = map.screen(c, r);
    if (!at)
        throw new Error(`tile ${c}, ${r} is not in sight`);
    return [at.x, at.y];
}

/** Clicks tile (c, r) for the placing step `step`. */
async function place(step: string, c: number, r: number): Promise<void> {
    pointer.move(...onView(c, r));
    await frames(2);
    expect(build.placing()?.step === step && build.placing()?.problem === "none", `${step} to place at ${c},${r}, not ${JSON.stringify(build.placing())}`);
    await pointer.click(...onView(c, r));
    await frames(2);
}

await look(col + 1, last, 40);
expect(build.pick(item.overlay, item.symbol), `${item.symbol} picked`);
await place("ride", col + ((item.width - 1) >> 1), first + ((item.length - 1) >> 1));
await place("entrance", col, last + 1);
await place("exit", right + 1, last);
await ui.click("info/close");
const ride = rides.list().length - 1;

// The stall south of the road.
const burger = build.stalls()!.find((s) => s.overlay === "Style/Vanilla/Style" && s.symbol.toLowerCase().includes("burger"));
expect(burger !== undefined, "a plain burger stall");
const stallCol = gate.col + 2;
await look(stallCol, road + 1, 40);
expect(build.pick(burger!.overlay, burger!.symbol), `${burger!.symbol} picked`);
await place("ride", stallCol, road + 1);
if (ui.find("info") !== undefined)
    await ui.click("info/close");
const stall = stalls.list().findIndex((s) => s.symbol === `${burger!.symbol}:sta`);
expect(stall >= 0, "the stall placed");
// Facings 0 to 3 put a stall's front towards -row, -col, +row and +col.
const kFront: [number, number][] = [
    [0, -1],
    [-1, 0],
    [0, 1],
    [1, 0],
];
const site = stalls.list()[stall].site!;
const front: [number, number] = [site.col + kFront[site.direction & 3][0], site.row + kFront[site.direction & 3][1]];

// Paths: the road from the gate, the way to the entrance, the way from the exit, and to the stall's front.
await ui.click("tools/Paths/Obj_Paths");
await until(() => ui.find("footpaths/route") !== undefined, 120, "the path panel");
await ui.toggle("footpaths/follow", false);
await ui.click("footpaths/queues");
await frames(2);
const queues = ui.children("footpaths/body") ?? [];
await ui.click("footpaths/paths");
await frames(2);
const styles = (ui.children("footpaths/body") ?? []).filter((n) => !queues.includes(n));
await ui.click(`footpaths/body/${styles.find((s) => s.toLowerCase().includes("asphalt")) ?? styles[0]}`);
await frames(2);
const has = (c: number, r: number) => map.paths().some((p) => p.col === c && p.row === r);
async function route(a: [number, number], b: [number, number]): Promise<void> {
    await look((a[0] + b[0]) >> 1, (a[1] + b[1]) >> 1, Math.max(60, 17 * (Math.abs(a[0] - b[0]) / 2 + 1), 8 * (Math.abs(a[1] - b[1]) / 2 + 1)));
    await ui.toggle("footpaths/route", false);
    await pointer.click(...onView(...a));
    await ui.toggle("footpaths/route", true);
    for (let wait = 1; wait <= 96 && !has(...b); wait *= 2) {
        pointer.move(...onView(...b));
        await frames(wait);
        await pointer.click(...onView(...b));
    }
    await ui.toggle("footpaths/route", false);
    expect(has(...a) && has(...b), `a path from ${a} to ${b}`);
}
await route([gate.col, gate.row + 1], [gate.col, road]);
await route([gate.col + 1, road], [right + 4, road]);
await route([col, last + 2], [col, road - 1]);
await route([right + 2, last], [right + 2, road - 1]);
if (!has(...front))
    await route(front, [front[0], road]);
await ui.click("footpaths/close");
await frames(3);

// The burger priced far above what anyone pays for one.
stalls.setPrice(stall, 0, 2000);
expect(stalls.list()[stall].items[0].price === 2000, `the burger at 20.00, not ${JSON.stringify(stalls.list()[stall].items)}`);
expect(stalls.setState(stall, "open") == null, "the stall opened");
const why = rides.setState(ride, "open");
expect(why == null, `the ride opened, not kept closed by ${why}`);
await park.advanceUntil(() => rides.list()[ride].joined === true && stalls.list()[stall].joined === true, 10, "the ride and the stall run by the park");

// The blank park's inspector has no tasks ticked: their window's tasks page ticks those to look at.
people.show(people.list().findIndex((p) => p.role === "inspector"));
await until(() => ui.find("info/Staff_Tools") !== undefined, 120, "the inspector's window");
await ui.click("info/Staff_Tools");
await frames(2);
for (const task of ["FlatRides", "RollerCoasters", "FoodDrinkStalls"])
    await ui.toggle(`info/body/StaffTasks_Evaluate_${task}`, true);
await ui.click("info/close");

// On the ride, seated and under way.
await park.advanceUntil(() => inspector()?.state === "riding" && inspector()?.ride !== undefined, 900, "the inspector riding");
log("riding", JSON.stringify(inspector()), "ride", JSON.stringify(rides.list()[ride]));
expect(inspector()!.ride === ride, `the inspector on our ride, not ${inspector()!.ride}`);
const seat = inspector()!;
camera.look({ col: seat.col!, row: seat.row! }, { col: seat.col! + 3, row: seat.row! + 5, up: 5 });
await frames(10);
await screenshot("inspector_riding");

// Too expensive, then at the counter.
const told = () => park.messages().some((m) => m.key === "Thought_PI_Stall_TooExpensive1");
await park.advanceUntil(() => told() && inspector()?.state === "buying", 900, "the inspector's word on the stall, and them at its counter");
log("messages", JSON.stringify(park.messages()));
const counter = inspector()!;
camera.look({ col: counter.col!, row: counter.row! }, { col: counter.col! + 3, row: counter.row! + 4, up: 4 });
await frames(10);
await screenshot("inspector_at_stall");

// The message, in the drawer: a double press opens it.
for (let tries = 0; tries < 3; ++tries) {
    const d = ui.find("messages")!;
    if (d.bottom - d.top > (d.right - d.left) / 2)
        break;
    await pointer.click(d.x, d.y);
    await pointer.click(d.x, d.y);
    await frames(30);
}
await screenshot("inspector_message");
quit(0);
