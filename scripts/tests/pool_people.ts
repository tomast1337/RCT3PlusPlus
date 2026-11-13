// SPDX-License-Identifier: GPL-3.0-or-later
//
// Workers and lines at a pool complex's rides. A mechanic sent to a broken slide goes no further than the path
// door of one of the complex's changing rooms and mends it from there. An inspector the park saved swimming
// stays in the complex, a swimmer on its paving or in its water, and no pool slide has a "no path to its
// entrance" said of it. Swimmers going up a slide stand in a line behind each other along their way to its
// platform, up the steps to one standing above the pool, and ride a slide that runs back up to its station.
// Give it a park the game saved with a broken pool slide, no mechanic, its inspector in the complex, a slide
// splashing out into the pool and one running round past the pool back up to its station:
//
//   ./nob script scripts/tests/pool_people.ts -- --no-intro --no-audio --seed 1 --park PARK

export {};

/** The path of the widget named `name` somewhere under `path`. */

await until(() => park.loaded(), 1200, "the park");
await frames(10);
const daylight = () => {
    park.setWeather("sunny");
    park.setTemperature(28);
    park.setTime(0.5);
};
daylight();
const { squares, rooms } = map.pools();
expect(rooms.length > 0, "a pool complex with changing rooms");
const inPool = (p: { col?: number; row?: number }) => p.col !== undefined && squares.some((s) => s.col === Math.floor(p.col!) && s.row === Math.floor(p.row!));
const slides = rides.list().map((r, i) => [r, i] as const).filter(([r]) => r.joined && !r.flat && !r.demolished && /slide|blaster/i.test(r.symbol));
const broken = slides.find(([r]) => !r.open);
const looping = slides.find(([r]) => r.open && !/blaster/i.test(r.symbol));
expect(broken !== undefined, "a pool slide broken down");
expect(looping !== undefined, "a slide running back up to its station");
const name = (i: number) => rides.list()[i].name;

// 1. The inspector, loaded where the park left them: swimming in the complex, on its paving or in its water.
const inspector = () => people.list().find((p) => p.staff && p.role === "inspector");
expect(inspector() !== undefined, "an inspector");
log("inspector", JSON.stringify(inspector()));
expect(inspector()!.state === "swimming" && inspector()!.swimwear === true && inPool(inspector()!), "the inspector swimming in the complex");
const i0 = inspector()!;
camera.look({ col: i0.col!, row: i0.row!, up: i0.up! }, { col: i0.col! + 6, row: i0.row! + 6, up: i0.up! + 8 });
await frames(10);
await screenshot("pool_people_inspector");
for (let k = 0; k < 20 && inspector()!.state === "swimming"; ++k) {
    await park.advance(2);
    const i = inspector()!;
    if (i.state !== "swimming")
        break;
    expect(inPool(i), `the inspector staying in the complex, not at ${i.col?.toFixed(1)},${i.row?.toFixed(1)}`);
    expect(i.in_water === true || Math.abs(i.up! - Math.round(i.up!)) < 0.5, `the inspector on the paving or in the water, not at height ${i.up}`);
}

// 2. A mechanic hired onto the path by a changing room: they go to a room's door and mend the slide from there.
await ui.click(`tools/Management/${must(ui.children("tools/Management"), "tools/Management shown")[0]}`);
await until(() => ui.find("tools/Management/Man_Staff") !== undefined, 120, "the management menu");
await ui.click("tools/Management/Man_Staff");
await until(() => under("Staff_Title", "Staff_HireMechanicIcon") !== undefined, 120, "the staff window's hire buttons");
const staffCount = people.list().filter((p) => p.staff).length;
await ui.click(must(under("Staff_Title", "Staff_HireMechanicIcon"), "Staff_HireMechanicIcon under Staff_Title"));
await until(() => people.list().filter((p) => p.staff).length === staffCount + 1, 60, "a mechanic hired");
const mechanic = () => people.list().filter((p) => p.staff && p.role === "mechanic")[0];
await until(() => mechanic().state === "held", 30, "the mechanic in hand");
const paths = map.paths().filter((p) => !p.queue && !p.raised);
const room = rooms[0];
const near = paths.map((p) => [p, Math.hypot(p.col - room.col, p.row - room.row)] as const).filter(([, d]) => d > 6).sort((a, b) => a[1] - b[1]);
expect(near.length > 0, "a path some way from the complex");
// The window out of the way, then put down on the path farthest from the complex, for a walk to watch.
await ui.click(under("Staff_Title", "close") ?? under("Staff_Title", "Close") ?? "Staff_Title");
await frames(5);
const drop = near[near.length - 1][0];
camera.look({ col: drop.col + 0.5, row: drop.row + 0.5 }, { col: drop.col + 8, row: drop.row + 8, up: 20 });
await frames(10);
const at = map.screen(drop.col + 0.5, drop.row + 0.5);
expect(at !== undefined, "the path in sight");
pointer.move(at!.x, at!.y);
await frames(5);
await pointer.click(at!.x, at!.y);
await until(() => mechanic().state !== "held", 60, "the mechanic put down");
await park.advanceUntil(() => mechanic().job === "fix", 120, "the mechanic sent to the slide");
const m0 = mechanic();
camera.look({ col: m0.col!, row: m0.row!, up: m0.up! }, { col: m0.col! + 6, row: m0.row! + 6, up: m0.up! + 8 });
await frames(10);
await screenshot("pool_people_mechanic_going");
// They stop at a changing room's door, out on the path, and stay there till the slide is mended.
const fromRoom = (m: { col?: number; row?: number }) => Math.min(...rooms.map((r) => Math.hypot(m.col! - r.col - 0.5, m.row! - r.row - 0.5)));
let last = mechanic();
await park.advanceUntil(() => {
    const m = mechanic();
    expect(!inPool(m), "the mechanic kept out of the complex");
    const still = m.job === "fix" && Math.hypot(m.col! - last.col!, m.row! - last.row!) < 0.01;
    last = m;
    return still;
}, 600, "the mechanic stopped to work");
const m1 = mechanic();
log("mechanic working at", m1.col?.toFixed(1), m1.row?.toFixed(1), m1.state, m1.job, "from a room", fromRoom(m1).toFixed(1));
expect(fromRoom(m1) < 1.5, "the mechanic at a changing room's door");
camera.look({ col: m1.col!, row: m1.row!, up: m1.up! }, { col: m1.col! - 4, row: m1.row! + 4, up: m1.up! + 5 });
daylight();
await frames(5);
await screenshot("pool_people_mechanic_repairing");
await park.advanceUntil(() => rides.list()[broken![1]].open === true, 300, `${name(broken![1])} mended and open`);
expect(!inPool(mechanic()), "the mechanic never in the complex");

// 3. Lines: those going up a slide stand one behind another along their way to its platform.
let best = 0;
await park.advanceUntil(() => {
    best = Math.max(best, rides.list()[broken![1]].queue ?? 0);
    return best >= 4;
}, 600, `a line at ${name(broken![1])}`);
// Those queueing in swimwear nearer this slide's platform than any other's.
const platform = (ride: number) => rides.list()[ride].platformSquares![0];
const line = (ride: number) =>
    people.list().filter((p) => {
        const gap = (i: number) => Math.hypot(p.col! - platform(i).col - 0.5, p.row! - platform(i).row - 0.5);
        return p.state === "queueing" && p.swimwear && slides.every(([, i]) => gap(ride) <= gap(i));
    });
const q = line(broken![1]);
log("line", q.length, q.map((p) => `${p.col?.toFixed(1)},${p.row?.toFixed(1)},${p.up?.toFixed(1)}`).join(" "));
const spread = Math.max(...q.map((a) => Math.max(...q.map((b) => Math.hypot(a.col! - b.col!, a.row! - b.row!)))));
expect(spread > (q.length - 1) * 0.1, "the line strung out a step apart, not all on one spot"); // Under a metre a step, four to a square
const first = q[0];
camera.look({ col: first.col!, row: first.row!, up: first.up! }, { col: first.col! + 2.5, row: first.row! + 2.5, up: first.up! + 3 });
await frames(10);
daylight();
await frames(5);
await screenshot("pool_people_line");

// 4. The slide running back up to its station gets riders, from the steps, and no "no path" is said of it.
let riders = 0;
await park.advanceUntil(() => {
    const r = rides.list()[looping![1]];
    riders = Math.max(riders, r.riders ?? 0);
    return (r.customers ?? 0) >= 3;
}, 600, `riders on ${name(looping![1])}`);
const loop = line(looping![1]);
log(name(looping![1]), "customers", rides.list()[looping![1]].customers, "riders", riders, "line", loop.map((p) => `${p.col?.toFixed(1)},${p.row?.toFixed(1)},${p.up?.toFixed(1)}`).join(" "));
if (loop.length > 0) {
    const p = loop[loop.length - 1];
    camera.look({ col: p.col!, row: p.row!, up: p.up! }, { col: p.col! - 3, row: p.row! - 2, up: p.up! + 3 });
    await frames(10);
    daylight();
    await frames(5);
    await screenshot("pool_people_steps_line");
}
const said = park.messages();
log("messages", JSON.stringify(said.slice(-10)));
expect(!said.some((m) => m.key === "Thought_PI_Ride_NoPathToEntrance1" && slides.some(([r]) => r.name === m.name)), "no slide said to have no path to its entrance");
