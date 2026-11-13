// SPDX-License-Identifier: GPL-3.0-or-later
//
// A lazy river is a pool ride: it has no entrance or exit, its stations join the pool paving, pool paths or path
// stairs beside them. Closed and opened again it opens; swimmers walk to the square beside a station, get on from
// it and get off onto it; riders keep floating round over time. A pool slide laid from a saved design away from
// the park's pools (its end in a pool of its own) asks for no entrance or exit and does not open till a station joins
// a pool. The river's window offers
// no entrance or exit. Give it a park the game saved with a lazy river joined to its pools and guests swimming:
//
//   ./nob script scripts/tests/pool_river_station.ts -- --no-intro --no-audio --seed 1 --park PARK

export {};

await until(() => park.loaded(), 1200, "the park");
await frames(10);
park.setWeather("sunny");
park.setTemperature(28);
park.setTime(0.5);
const kPool = "Pool/Default/Default_Stub";
let here: [number, number] = [0, 0]; // Where the pointer was last moved for a ghost
const kSteps: [number, number][] = [
    [0, 1],
    [1, 0],
    [0, -1],
    [-1, 0],
]; // By side, as the pools count them

/** Moves the pointer until the design in hand has its first piece on tile `goal`; whether it does. */
async function aim(goal: { col: number; row: number }): Promise<boolean> {
    let at = { ...goal };
    for (let i = 0; i < 4; ++i) {
        camera.look({ col: at.col, row: at.row }, { col: at.col, row: at.row + 30, up: 45 });
        await frames(4);
        const p = must(map.screen(at.col, at.row), `tile ${at.col}, ${at.row} in sight`);
        here = [p.x, p.y];
        pointer.move(...here);
        await frames(4);
        const g = build.construction()?.ghostAt;
        if (!g) return false;
        if (g.col === goal.col && g.row === goal.row) return true;
        at = { col: at.col + goal.col - g.col, row: at.row + goal.row - g.row };
    }
    return false;
}

/** With a slide's design in hand where only its pool sides keep it from standing: water laid with the pool tool on
 * each tile across them, then the design picked again by `pick`, turned and aimed as before. Whether it may stand. */
async function byPool(pick: () => Promise<void>): Promise<boolean> {
    const c = build.construction()!;
    const at = c.ghostAt!;
    const turn = c.turn;
    const across = c.poolSides.map((s) => ({ col: s.col + kSteps[s.side][0], row: s.row + kSteps[s.side][1] }));
    await ui.click("design_placing/close");
    await frames(5);
    const rides_tab = `tools/Rides/${must(ui.children("tools/Rides"), "tools/Rides shown")[0]}`;
    if (!ui.find("tools/Rides/Obj_Pools")) await ui.click(rides_tab);
    await until(() => ui.find("tools/Rides/Obj_Pools") !== undefined, 120, "the rides menu");
    await ui.click("tools/Rides/Obj_Pools");
    await until(() => ui.find("pool_panel/body/editor/Pool_PlaceWater") !== undefined, 120, "the pool panel");
    expect(build.pick(kPool, "Pool_DepthNormal"), "the water in the pool list");
    for (const t of across) {
        camera.look({ col: t.col, row: t.row }, { col: t.col, row: t.row + 30, up: 45 });
        await frames(4);
        const p = must(map.screen(t.col, t.row), `tile ${t.col}, ${t.row} in sight`);
        await pointer.drag([p.x, p.y], [p.x, p.y], 4);
        await frames(3);
    }
    build.stop();
    await frames(5);
    await pick();
    for (let i = 0; i < turn; ++i) {
        await ui.click("design_placing/IconAttractionEditorRotate");
        await frames(2);
    }
    return (await aim(at)) && build.construction()?.refusal === "";
}

const rivers = () =>
    rides.list().flatMap((r, i) => (!r.flat && !r.demolished && /lazyriver/i.test(r.symbol) ? [i] : []));
const river = rivers()[0];
expect(river !== undefined, "a lazy river in the park");
const info = () => rides.list()[river];

// 1. Closed and opened again, it opens: its stations joined to the pools stand for its doors.
expect(rides.setState(river, "closed") == null, "the river closed");
await frames(5);
const refused = rides.setState(river, "open");
expect(refused == null, `the river opened, not refused with ${refused}`);
await until(() => (info().riverJoined ?? 0) > 0, 60, "a station joined to a pool complex");
const squares = info().riverSquares!;
log(
    "river",
    info().name,
    "stations",
    info().riverStations,
    "joined",
    info().riverJoined,
    "squares",
    JSON.stringify(squares),
);
expect(info().entrance == null && info().exit == null, "no entrance or exit");
const near = (p: { col?: number; row?: number }, by: number) =>
    squares.some((s) => Math.hypot(p.col! - s.col, p.row! - s.row) <= by);
const square = squares[0];
camera.look(
    { col: square.col, row: square.row, up: square.up },
    { col: square.col + 4, row: square.row + 4, up: square.up + 6 },
);
await frames(20);
await screenshot("river_station_joined");

// 2. Swimmers get on from the square beside a station, and riders get off onto one; riders keep floating round.
const floating = () => people.list().filter((p) => p.floating !== undefined && p.ride === river);
const before = new Map<string, { col?: number; row?: number }>();
const boarded: string[] = [];
const off: string[] = [];
const riders: number[] = [];
let aboard = new Set(floating().map((p) => p.name));
let boarding_shot = false;
for (let second = 0; second < 600 && (boarded.length < 2 || off.length < 2); ++second) {
    await park.advance(1);
    // Known by name; the few names two people share are left out.
    const all = people.list();
    const named = new Map<string, number>();
    for (const p of all) named.set(p.name, (named.get(p.name) ?? 0) + 1);
    const now = all.filter((p) => named.get(p.name) === 1);
    const floating_now = new Set(now.filter((p) => p.floating !== undefined && p.ride === river).map((p) => p.name));
    for (const p of now) {
        if (floating_now.has(p.name) && !aboard.has(p.name) && before.has(p.name)) {
            // Where they stood a second ago, before getting on: by the square beside the station.
            const was = before.get(p.name)!;
            log("on", p.name, "from", was.col?.toFixed(2), was.row?.toFixed(2));
            expect(near(was, 1.5), `${p.name} on from beside a joined square, not from ${JSON.stringify(was)}`);
            boarded.push(p.name);
            if (!boarding_shot) {
                camera.look(
                    { col: square.col, row: square.row, up: square.up },
                    { col: square.col + 3, row: square.row + 3, up: square.up + 4 },
                );
                await frames(10);
                await screenshot("river_boarding");
                boarding_shot = true;
            }
        }
        if (!floating_now.has(p.name) && aboard.has(p.name) && p.state === "swimming") {
            log("off", p.name, "at", p.col?.toFixed(2), p.row?.toFixed(2));
            expect(near(p, 0.6), `${p.name} off onto a joined square, not at ${p.col}, ${p.row}`);
            expect(p.swimwear === true, `${p.name} still in their swimwear`);
            off.push(p.name);
        }
        before.set(p.name, { col: p.col, row: p.row });
    }
    aboard = floating_now;
    if (second % 30 === 0) riders.push(info().riders ?? 0);
}
log("riders every 30 s", riders.join(" "), "boarded", boarded.length, "off", off.length, "customers", info().customers);
expect(boarded.length > 0, "a swimmer getting on from beside a station");
expect(off.length > 0, "a rider getting off onto a joined square");
expect(
    riders.every((n) => n > 0),
    `riders floating round all along, not ${riders.join(" ")}`,
);
expect(info().open === true, "the river still open");

// 3. A pool ride laid from a saved design away from the park's pools, a ring slide (the river's own saved design does not
// load as a design yet): no entrance or exit is asked for once it is laid, and it does not open till a station joins
// a pool.
await until(() => build.flatRides() !== undefined, 1200, "the build lists");
const pick = async () => {
    expect(
        build.pick("tracks/TrackedRides/RingSlide/RingSlide", "RingSlide", "designs"),
        "a ring slide design to build",
    );
    await until(() => ui.find("design_browser") !== undefined, 600, "the designs window");
    await frames(3);
    const row = build.construction()!.designs.findIndex((d) => d.includes("Undertow"));
    expect(row >= 0, `the Undertow design among ${build.construction()!.designs.join(", ")}`);
    const list = must(ui.find("design_browser/content/list"), "design_browser/content/list shown");
    const button = must(ui.find("design_browser/content/build"), "design_browser/content/build shown");
    const unit = (button.bottom - button.top) / 22;
    await pointer.click(list.x, list.top + (18 + 16 * row) * unit);
    await frames(5);
    await ui.click("design_browser/content/build");
    await frames(10);
};
await pick();
camera.look({ col: 20, row: 20, up: 0 }, { col: 20, row: 50, up: 60 });
await frames(10);
// Open ground where only its pool sides keep it from standing, then a pool of its own laid across them.
let spot = false;
for (let k = 0; k < 60 && !spot; ++k) {
    here = [0.05 + 0.06 * (k % 15), 0.3 + 0.1 * Math.floor(k / 15)];
    pointer.move(...here);
    await frames(3);
    const c = build.construction();
    spot = c !== undefined && c.ghost && (c.refusal === "" || c.refusal === "PlaceObject_MustEndOnPoolEdge");
}
expect(spot && (build.construction()!.refusal === "" || (await byPool(pick))), "clear ground for the design");
await pointer.click(...here);
await frames(30);
await screenshot("slide_laid");
const built = rides.list().length - 1;
expect(/ringslide/i.test(rides.list()[built].symbol) && rides.list()[built].pieces! > 1, "the slide laid");
expect(build.placing() === undefined, `no entrance or exit asked for, not ${JSON.stringify(build.placing())}`);
// Its construction panel shut: taking out the ground tool and putting it away shuts it.
await ui.click("tools/Land/Land");
await frames(3);
await ui.click("tools/Land/Land_Brush_Height");
await until(() => ui.find("land_panel/Land_Vertex") !== undefined, 60, "the land panel");
await frames(5);
await ui.click("tools/Land/Land_Brush_Height");
await frames(5);
expect(build.construction() === undefined, "the construction panel shut");
const why = rides.setState(built, "open");
log("laid", rides.list()[built].name, "pieces", rides.list()[built].pieces, "open:", why ?? "yes");
expect(why === "CantOpenPoolRideNoEntrance", `the slide refused till joined to a pool, not ${why}`);
expect(rides.list()[built].entrance == null, "the slide with no entrance");

// 4. Its window offers no entrance or exit to place.
camera.look(
    { col: square.col, row: square.row, up: square.up },
    { col: square.col + 6, row: square.row + 6, up: square.up + 10 },
);
rides.show(river);
await until(() => rides.shown()?.ride === river, 60, "the river's window");
await frames(10);
expect(
    ui.find("info/body/Attraction_General_Entrance") === undefined &&
        ui.find("info/body/Attraction_General_Exit") === undefined,
    "no entrance or exit buttons for the river",
);
await screenshot("river_window");
