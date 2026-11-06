// SPDX-License-Identifier: GPL-3.0-or-later
//
// Slides that end in a pool. A slide's open end joined to a pool leaves the pool's railing open across from it,
// and pulled down, the railing is back. The Undertow ring slide runs its car back through the pool to its lift:
// laid beside a pool of its own, its car is slowed in the water and carried on, never stopping. Give it a park the
// game saved with a body, ring or pro slide ending in a pool:
//
//   ./nob script scripts/tests/slide_pool_end.ts -- --no-intro --no-audio --seed 1 --park PARK

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
const kSteps: [number, number][] = [[0, 1], [1, 0], [0, -1], [-1, 0]]; // By side, as the pool tool counts them

await until(() => park.loaded(), 1200, "the park");
await frames(10);
park.setWeather("sunny");
park.setTime(0.5);

// Water squares with fewer railings round them than sides with no square beside at their level: where something
// joins the pool. Squares by a changing room or a ramp are left out, those open there too.
const missing = () => {
    const { squares, ramps, rooms } = map.pools();
    const railings = map.objects().filter((o) => o.symbol === "Railing");
    const out: { col: number; row: number; up: number }[] = [];
    for (const s of squares) {
        if (s.kind !== "water")
            continue;
        const beside = (dc: number, dr: number) => squares.some((o) => o.col === s.col + dc && o.row === s.row + dr && o.surface === s.surface);
        const near = (t: { col: number; row: number }) => Math.abs(t.col - s.col) + Math.abs(t.row - s.row) <= 1;
        if (rooms.some(near) || ramps.some(near))
            continue;
        const want = kSteps.filter(([dc, dr]) => !beside(dc, dr)).length;
        const have = railings.filter((r) => r.col === s.col && r.row === s.row && r.height === s.surface).length;
        if (have < want)
            out.push({ col: s.col, row: s.row, up: s.surface });
    }
    return out;
};

// The saved slide's end joins the pool: the one square across from it open.
const slide = rides.list().findIndex((r) => !r.flat && !r.demolished && /(body|ring|pro)slide/i.test(r.symbol));
expect(slide >= 0, "a pool slide in the park");
const open_end = missing();
log("slide", rides.list()[slide].name, "last piece", rides.list()[slide].track?.at(-1), "open squares", JSON.stringify(open_end));
expect(open_end.length === 1, `one square open to the slide's end, not ${JSON.stringify(open_end)}`);
const end = open_end[0];
camera.look({ col: end.col + 0.5, row: end.row + 0.5, up: end.up }, { col: end.col + 4.5, row: end.row + 3.5, up: end.up + 4 });
await frames(20);
await screenshot("slide_pool_end_open");

// The Undertow laid on open ground, tested with no pool.
const rides_tab = `tools/Rides/${ui.children("tools/Rides")![0]}`;
await until(() => build.flatRides() !== undefined, 1200, "the build items");
const { width, height } = map.size();
camera.look({ col: width / 2, row: height / 2 }, { col: width / 2, row: height / 2 + 30, up: 40 });
await frames(10);
expect(build.pick("tracks/TrackedRides/RingSlide/RingSlide", "RingSlide", "designs"), "a ring slide to build");
await until(() => ui.find("design_browser") !== undefined, 600, "the designs window");
await frames(3);
const row = build.construction()!.designs.findIndex((d) => d.includes("Undertow"));
expect(row >= 0, "the Undertow design");
const list = ui.find("design_browser/content/list")!;
const button = ui.find("design_browser/content/build")!;
const unit = (button.bottom - button.top) / 22;
await pointer.click(list.x, list.top + (18 + 16 * (row + 1) + 8) * unit);
await frames(5);
await ui.click("design_browser/content/build");
await frames(10);
let spot: [number, number] | undefined;
for (let k = 0; k < 60 && spot === undefined; ++k) {
    const at: [number, number] = [0.05 + 0.06 * (k % 15), 0.3 + 0.1 * Math.floor(k / 15)];
    pointer.move(...at);
    await frames(3);
    const c = build.construction();
    if (c !== undefined && c.ghost && !c.clashes)
        spot = at;
}
expect(spot !== undefined, "open ground for the Undertow");
await pointer.click(...spot!);
await frames(30);
build.stop();
await frames(10);
const undertow = rides.list().length - 1;
const info = () => rides.list()[undertow];
log("undertow", info().name, "pieces", info().pieces);
expect(info().pieces === 56, `the Undertow laid whole, not ${info().pieces} pieces`);

// Tested: each car's lowest stretch, its way back past the slide's end, and how fast it goes there.
const run = async (seconds: number) => {
    const seen: { col: number; row: number; up: number; speed: number; front: number }[] = [];
    for (let k = 0; k < seconds * 10; ++k) {
        await park.advance(0.1);
        const cars = info().cars ?? [];
        (info().trainsAt ?? []).forEach((t, i) => {
            if (cars[i] !== undefined && t.station === null)
                seen.push({ col: cars[i].col, row: cars[i].row, up: cars[i].up ?? 0, speed: t.speed, front: t.front });
        });
    }
    return seen;
};
const refused = rides.setState(undertow, "testing");
log("undertow tested with no pool:", refused ?? "not refused");
expect(refused == null, `the Undertow tested, not refused with ${refused}`);
const dry = await run(90);
expect((info().crashes ?? 0) === 0, "no crash");
const lowest = Math.min(...dry.map((s) => s.up));
const dip = dry.filter((s) => s.up < lowest + 0.6);
const cols = dip.map((s) => Math.floor(s.col));
const rows = dip.map((s) => Math.floor(s.row));
const box = { c0: Math.min(...cols), c1: Math.max(...cols), r0: Math.min(...rows), r1: Math.max(...rows) };
log("its way back dips to", lowest.toFixed(2), "over tiles", JSON.stringify(box), "at", Math.min(...dip.map((s) => s.speed)).toFixed(2), "to", Math.max(...dip.map((s) => s.speed)).toFixed(2), "m/s");
// Along the track, the stretch down in the water.
const dipped = { from: Math.min(...dip.map((s) => s.front)), to: Math.max(...dip.map((s) => s.front)) };
const in_dip = (front: number) => front >= dipped.from && front <= dipped.to;
rides.setState(undertow, "closed");
await park.advance(1);

// A pool laid where its car goes back: water over the tiles it dips into.
if (!ui.find("tools/Rides/Obj_Pools"))
    await ui.click(rides_tab);
await until(() => ui.find("tools/Rides/Obj_Pools") !== undefined, 120, "the rides menu");
await ui.click("tools/Rides/Obj_Pools");
await until(() => ui.find("pool_panel/body/editor/Pool_PlaceWater") !== undefined, 120, "the pool panel");
camera.look({ col: (box.c0 + box.c1) / 2 + 0.5, row: (box.r0 + box.r1) / 2 + 0.5 }, { col: (box.c0 + box.c1) / 2 + 0.5, row: (box.r0 + box.r1) / 2 + 12, up: 14 });
await frames(10);
expect(build.pick(kPool, "Pool_DepthNormal"), "the water in the pool list");
await pointer.drag(on_view(box.c0 + 0.5, box.r0 + 0.5), on_view(box.c1 + 0.5, box.r1 + 0.5), 8);
await frames(5);
build.stop();
await frames(10);
const joined = missing().filter((s) => s.col >= box.c0 - 1 && s.col <= box.c1 + 1 && s.row >= box.r0 - 1 && s.row <= box.r1 + 1);
log("pool squares open to the Undertow", JSON.stringify(joined));
expect(joined.length > 0, "the new pool's railing open where the Undertow runs out over it");

// Tested again: down into the water off the slide, slowed, carried through and on up its lift.
expect(rides.setState(undertow, "testing") == null, "the Undertow tested");
const wet = await run(10);
let shown = false;
for (let k = 0; k < 3000 && !shown; ++k) {
    await park.advance(0.1);
    const cars = info().cars ?? [];
    const t = (info().trainsAt ?? []).findIndex((t, i) => cars[i] !== undefined && t.station === null && in_dip(t.front));
    if (t >= 0) {
        const car = { col: cars[t].col, row: cars[t].row, up: cars[t].up ?? 0 };
        camera.look({ col: car.col, row: car.row, up: car.up }, { col: car.col + 3, row: car.row + 3, up: car.up + 3 });
        await frames(4);
        await screenshot("slide_pool_end_undertow_wading");
        shown = true;
    }
}
expect(shown, "a car down in the pool");
const more = await run(60);
const wading = [...wet, ...more].filter((s) => in_dip(s.front));
log("in the pool at", Math.min(...wading.map((s) => s.speed)).toFixed(2), "to", Math.max(...wading.map((s) => s.speed)).toFixed(2), "m/s");
expect(wading.length > 0 && wading.every((s) => s.speed > 2.5 && s.speed < 3.5), "cars carried through the pool at about 3 m/s");
expect((info().crashes ?? 0) === 0, "no crash");
camera.look({ col: joined[0].col + 0.5, row: joined[0].row + 0.5, up: joined[0].up }, { col: joined[0].col + 5, row: joined[0].row + 4, up: joined[0].up + 6 });
await frames(20);
await screenshot("slide_pool_end_undertow_pool");

// The first slide pulled down: the pool's railing is back across from where its end stood.
rides.show(slide);
await frames(10);
await ui.click("info/body/Attraction_General_Delete");
await frames(2);
await ui.click("demolish_question/content/GUI2MiniDialog_Confirm");
await frames(10);
expect(rides.list()[slide].demolished, "the slide pulled down");
expect(!missing().some((s) => s.col === end.col && s.row === end.row), "the railing back where the slide ended");
camera.look({ col: end.col + 0.5, row: end.row + 0.5, up: end.up }, { col: end.col + 4.5, row: end.row + 3.5, up: end.up + 4 });
await frames(20);
await screenshot("slide_pool_end_closed");
log("SLIDE_POOL_END ok");
