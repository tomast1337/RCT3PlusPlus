// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pool slides put to test and opened: the car waiting at the top sets off empty, slides down and goes off the open
// end out of sight, with no crash, no wreck and no message; the next waiting car takes its place. Given a park
// (--park), its pool slides; without one, a saved design of each kind of pool slide laid on the blank landscape, with
// pool water put down first where its pool sides must meet one.
//
//   ./nob script scripts/tests/slide_test.ts -- --no-intro --no-audio --seed 1 [--park PARK]

export {};

const kPool = "Pool/Default/Default_Stub";
let here: [number, number] = [0, 0]; // Where the pointer was last moved for a ghost
const kSteps: [number, number][] = [[0, 1], [1, 0], [0, -1], [-1, 0]]; // By side, as the pools count them

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
        if (!g)
            return false;
        if (g.col === goal.col && g.row === goal.row)
            return true;
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
    if (!ui.find("tools/Rides/Obj_Pools"))
        await ui.click(rides_tab);
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

const kSlides = /^(BodySlide|RingSlide|ProSlide|MasterBlaster):/;
// Those with track: a design put away to lay water for it leaves its ride with none.
const slides = () => rides.list().flatMap((r, i) => (!r.flat && !r.demolished && (r.pieces ?? 0) > 0 && kSlides.test(r.symbol) ? [i] : []));

await frames(300);
let names: string[] = [];
if (park.loaded()) {
    names = slides().map((i) => rides.list()[i].symbol);
}
else {
    // A saved design of each kind, laid where its ghost fits.
    await park.open("Campaigns/Base/BlankLandscape.dat");
    await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "tools");
    for (const [kind, design] of [["BodySlide", "Reef Rider"], ["RingSlide", "Undertow"], ["ProSlide", "The Vortex"], ["MasterBlaster", "Tubular"]]) {
        const pick = async () => {
            expect(build.pick(`tracks/TrackedRides/${kind}/${kind}`, kind, "designs"), `a ${kind} to build`);
            await until(() => ui.find("design_browser") !== undefined, 600, "the designs window");
            await frames(3);
            const row = build.construction()!.designs.findIndex((d) => d.includes(design));
            expect(row >= 0, `the ${design} design among ${build.construction()!.designs.join(", ")}`);
            const list = must(ui.find("design_browser/content/list"), "design_browser/content/list shown");
            const button = must(ui.find("design_browser/content/build"), "design_browser/content/build shown");
            const unit = (button.bottom - button.top) / 22;
            await pointer.click(list.x, list.top + (18 + 16 * row) * unit);
            await frames(5);
            await ui.click("design_browser/content/build");
            await frames(10);
        };
        await pick();
        // Open ground where only its pool sides keep it from standing, then water laid across them for it.
        let spot = false;
        for (let k = 0; k < 60 && !spot; ++k) {
            here = [0.05 + 0.06 * (k % 15), 0.3 + 0.1 * Math.floor(k / 15)];
            pointer.move(...here);
            await frames(3);
            const c = build.construction();
            spot = c !== undefined && c.ghost && (c.refusal === "" || c.refusal === "PlaceObject_MustEndOnPoolEdge");
        }
        expect(spot && (build.construction()!.refusal === "" || (await byPool(pick))), `clear ground for the ${kind}`);
        await pointer.click(...here);
        await frames(30);
        const laid = rides.list()[rides.list().length - 1];
        log(kind, design, "pieces", laid.pieces, "track", (laid.track ?? []).join(" "));
        names.push(kind);
    }
    // The last one's construction panel shut, as when the player tests from the ride's window: taking out the ground
    // tool and putting it away shuts it.
    await ui.click("tools/Land/Land");
    await frames(3);
    await ui.click("tools/Land/Land_Brush_Height");
    await until(() => ui.find("land_panel/Land_Vertex") !== undefined, 60, "the land panel");
    await frames(5);
    await ui.click("tools/Land/Land_Brush_Height"); // Put away
    await frames(5);
    expect(build.construction() === undefined, "the construction panel shut");
}
const found = slides();
log("pool slides", found.map((i) => `${rides.list()[i].name} ${rides.list()[i].symbol}`).join(", "));
expect(found.length > 0 && found.length >= names.length, `a pool slide to test, not ${found.length} of ${names.join(" ")}`);

const messages = park.messages().length;
for (const slide of found) {
    const info = () => rides.list()[slide];
    const kind = info().symbol.split(":")[0];
    expect(rides.setState(slide, "closed") == null, `${kind} closed`);
    await frames(10);
    expect(rides.setState(slide, "testing") == null, `${kind} tested`);
    await frames(10);
    const length = info().trackLength ?? 0;
    expect(length > 0 && (info().trainsAt ?? []).length > 0, `${kind} run on its track by the park, not ${JSON.stringify(info())}`);
    // Ending in an open end, one car waits at the top and any others are out of sight past that end. A ring slide
    // may instead go round, carried back up by a lift, as a coaster does.
    const at = info().trainsAt!;
    const open_end = /^(EndStraight|Bowl|Funnel)/.test(info().track!.at(-1) ?? "");
    log(kind, "length", length.toFixed(1), "open end", open_end, "trains", JSON.stringify(at.map((t) => t.front.toFixed(1))));
    expect(!open_end || at.filter((t) => t.front < length - 0.01).length === 1, `one car on ${kind}'s track, the rest past its end, not ${JSON.stringify(at)}`);

    // Setting off: eased off the platform, not shot out of it. Its speed each tenth of a second for its first two
    // seconds under way (logged), and the speed it has left the platform at.
    const top = at.findIndex((t) => t.front < length - 0.01);
    await park.advanceUntil(() => info().trainsAt![top].speed > 0, 60, `${kind}'s car setting off`);
    const start = info().trainsAt![top].front;
    const speeds: string[] = [];
    for (let k = 0; k < 20; ++k) {
        const t = info().trainsAt![top];
        speeds.push(`${t.speed.toFixed(2)}@${(t.front - start).toFixed(2)}`);
        await park.advance(0.1);
    }
    log(kind, "setting off, speed@metres", speeds.join(" "));
    // A slide's car is eased off at a walk, not carried at the station's speed; one that goes round leaves at that.
    if (open_end)
        expect(Number(speeds[0].split("@")[0]) <= 1.01, `${kind}'s car eased off its platform, not ${speeds[0]}`);
    // The Aqua Blaster's jets shove its car up each climb they line.
    if (kind === "MasterBlaster") {
        let fastest = 0;
        const climb: string[] = [];
        for (let k = 0; k < 120; ++k) {
            const t = info().trainsAt![top];
            fastest = Math.max(fastest, t.speed);
            if (k % 4 === 0)
                climb.push(`${t.speed.toFixed(1)}@${t.front.toFixed(0)}`);
            await park.advance(0.25);
        }
        log(kind, "under way, speed@front", climb.join(" "), "fastest", fastest.toFixed(1));
        expect(fastest > 8, `${kind}'s car shoved up its climbs by the jets, not ${fastest.toFixed(1)} m/s at most`);
    }

    // Under way down the slide: a look at the empty car.
    const sliding = () => (info().trainsAt ?? []).findIndex((t) => t.station === null && t.front > 5 && t.front < length - 5);
    await park.advanceUntil(() => sliding() >= 0 && info().trainsAt![sliding()].front > length / 3, 120, `${kind}'s car on its way down`);
    const down = sliding();
    const car = info().cars![down];
    camera.look(car, { col: car.col + 3, row: car.row + 3, up: car.up! + 4 });
    await frames(10);
    await screenshot(`${kind}_testing`);

    // Off the end it is gone from sight, the next car coming down after it; none of it is a crash. On its way it
    // swings up the trough's walls in the turns (logged: the most, in radians, and where), short of going over.
    let most = 0;
    let most_at = 0;
    const swing = () => {
        const t = info().trainsAt![down];
        if (t.lean !== null && Math.abs(t.lean) > Math.abs(most)) {
            most = t.lean;
            most_at = t.front;
        }
        return Math.abs(t.lean ?? 0) > 0.3;
    };
    let last = info().trainsAt![down].front;
    let over = false;
    const done = () => {
        const now = info().trainsAt![down].front;
        over ||= (info().crashes ?? 0) > 0 || (open_end ? now >= length - 0.01 : now < last);
        last = now;
        return over;
    };
    // Stopped for a look the first time it is well up a wall.
    await park.advanceUntil(() => swing() || done(), 240, `${kind}'s car up a wall or round`);
    if (swing()) {
        const c = info().cars![down];
        camera.look(c, { col: c.col + 2, row: c.row + 2, up: c.up! + 2 });
        await frames(5);
        await screenshot(`${kind}_up_the_wall`);
    }
    await park.advanceUntil(() => { swing(); return done(); }, 240, open_end ? `${kind}'s car off the end` : `${kind}'s car round past its station`);
    log(kind, "swung up the wall at most", most.toFixed(2), "at", most_at.toFixed(1));
    await park.advance(20);
    expect((info().crashes ?? 0) === 0, `no crash on ${kind}, not ${info().crashes}`);
    expect((info().wrecks ?? []).length === 0, `no wrecks on ${kind}`);
    expect((info().moving ?? 0) > 0, `${kind}'s cars still going round`);

    // Opened, it runs on with no crash either; laid on dry land, away from a pool, it may not open.
    const refused = rides.setState(slide, "open");
    log(kind, "opened", refused ?? "yes");
    if (refused != null)
        continue;
    await park.advance(30);
    expect((info().crashes ?? 0) === 0 && (info().wrecks ?? []).length === 0, `no crash on ${kind} once open`);
    expect(info().open === true, `${kind} still open`);
}
const told = park.messages().slice(messages).map((m) => m.key);
expect(!told.some((k) => /crash/i.test(k)), `no crash message, not ${told.join(" ")}`);

// Too fast for its turns: a ring slide's car driven round at a speed far past what its drops give flies out of its
// trough in the first tight turn and crashes, as a coaster's train off its track does.
const ring = found.find((i) => /^RingSlide:/.test(rides.list()[i].symbol));
if (ring !== undefined) {
    const info = () => rides.list()[ring];
    expect(rides.setState(ring, "closed") == null, "the ring slide closed");
    await frames(10);
    expect(rides.setState(ring, "testing") == null, "the ring slide tested");
    await frames(10);
    rides.setFixedSpeed(ring, 25);
    let most = 0;
    let car = 0;
    await park.advanceUntil(() => {
        info().trainsAt!.forEach((t, k) => {
            if (Math.abs(t.lean ?? 0) > most) {
                most = Math.abs(t.lean ?? 0);
                car = k;
            }
        });
        return (info().crashes ?? 0) > 0;
    }, 60, "the ring slide's car flying out of its trough");
    log("RingSlide at 25 m/s flew out at", info().trainsAt![car].front.toFixed(1), "swung", most.toFixed(2));
    const c = info().cars?.[car];
    if (c !== undefined) {
        camera.look(c, { col: c.col + 3, row: c.row + 3, up: (c.up ?? 0) + 4 });
        await frames(10);
        await screenshot("RingSlide_flown_out");
    }
    rides.setFixedSpeed(ring, 0);
}
