// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every saved design of a pool slide the game ships, laid on the blank landscape, tested and then opened: each is laid
// whole, with pool water put down first where its pool sides must meet one, and runs several cars down with none
// crashing. Logs for each design its pieces, its cars' top speed, the most
// they swing up a trough's wall and any crash, and each train's place, speed and swing through its test.
//
//   ./nob script scripts/tests/slide_designs.ts -- --no-intro --no-audio --seed 1

export {};

// Each with the pieces its file holds, so the one laid is known to be the one picked.
const kDesigns: [string, string, number][] = [
    ["BodySlide", "Crossover", 33], ["BodySlide", "DareDevil", 23], ["BodySlide", "Flyover Falls", 33], ["BodySlide", "Reef Rider", 21],
    ["BodySlide", "Splashback", 29], ["BodySlide", "Torpedo", 25],
    ["RingSlide", "Seawave", 54], ["RingSlide", "Splash Down", 34], ["RingSlide", "Undertow", 56],
    ["ProSlide", "Drop Out", 18], ["ProSlide", "Orange Bowl", 27], ["ProSlide", "The Frightful Funnel", 21], ["ProSlide", "The Juicer", 23],
    ["ProSlide", "The Vortex", 9],
    ["MasterBlaster", "Aqua Launch", 26], ["MasterBlaster", "Blasted", 61], ["MasterBlaster", "Journey to the deep", 73],
    ["MasterBlaster", "Slipstream", 69], ["MasterBlaster", "Tubular", 63],
];

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

const failed: string[] = [];
let shown: string | undefined;
for (const [kind, design, size] of kDesigns) {
    await park.open("Campaigns/Base/BlankLandscape.dat");
    await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "tools");
    const pick = async () => {
        expect(build.pick(`tracks/TrackedRides/${kind}/${kind}`, kind, "designs"), `a ${kind} to build`);
        await until(() => ui.find("design_browser") !== undefined, 600, "the designs window");
        await frames(3);
        const row = build.construction()!.designs.findIndex((d) => d.includes(design));
        expect(row >= 0, `the ${design} design among ${build.construction()!.designs.join(", ")}`);
        const list = must(ui.find("design_browser/content/list"), "design_browser/content/list shown");
        const button = must(ui.find("design_browser/content/build"), "design_browser/content/build shown");
        const unit = (button.bottom - button.top) / 22;
        // Under the column heads and the custom track's row, which stays first.
        await pointer.click(list.x, list.top + (18 + 16 * (row + 1) + 8) * unit);
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
    if (!spot || (build.construction()!.refusal !== "" && !(await byPool(pick)))) {
        log("DESIGN", kind, design, "no clear ground", build.construction()?.refusal);
        failed.push(`${design}: no clear ground`);
        if (build.construction() !== undefined)
            await ui.click("design_placing/close");
        continue;
    }
    // Laid, its building closes and its window opens.
    await pointer.click(...here);
    await frames(30);
    // The construction panel shut, as when the player tests from the ride's window: taking out the ground tool and
    // putting it away shuts it.
    await ui.click("tools/Land/Land");
    await frames(3);
    await ui.click("tools/Land/Land_Brush_Height");
    await until(() => ui.find("land_panel/Land_Vertex") !== undefined, 60, "the land panel");
    await frames(5);
    await ui.click("tools/Land/Land_Brush_Height");
    await frames(5);
    const slide = rides.list().length - 1;
    const info = () => rides.list()[slide];
    const track = info().track ?? [];
    const open_end = /^(EndStraight|Bowl|Funnel)/.test(track.at(-1) ?? "");
    log("DESIGN", kind, design, "pieces", info().pieces, "first", track[0], "last", track.at(-1), "open end", open_end);

    // Tested, then opened: several cars down (or round), watched each quarter second.
    let top = 0;
    let lean = 0;
    const watch = () => {
        for (const t of info().trainsAt ?? []) {
            top = Math.max(top, Math.abs(t.speed));
            lean = Math.max(lean, Math.abs(t.lean ?? 0));
        }
    };
    // Each thirtieth of a second on test, every train's place, speed and swing, as "TRACE" lines to set beside other runs.
    let state = rides.setState(slide, "testing");
    for (let k = 0; k < 2700 && state == null; ++k) {
        await park.advance(1 / 30);
        watch();
        for (const [i, t] of (info().trainsAt ?? []).entries())
            log("TRACE", `${kind}/${design}`, (k / 30).toFixed(3), i, t.front.toFixed(3), t.speed.toFixed(3), (t.lean ?? 0).toFixed(4), t.station ?? "-");
    }
    const tested = info().crashes ?? 0;
    const refused = rides.setState(slide, "open");
    for (let k = 0; k < 240 && refused == null; ++k) {
        await park.advance(0.25);
        watch();
    }
    // The first with an open end, still on test: a look from the side where its cars come down off it, the water pouring off its lip
    // beyond, as a car flies off and lands.
    if (open_end && state == null && shown === undefined) {
        const landing = () => map.effects().find((e) => /^(splash2|waterwakemedium)$/.test(e.effect));
        await until(() => landing() !== undefined, 2400, "a car down off the end");
        const at = landing()!;
        shown = design;
        camera.look({ col: at.col, row: at.row, up: at.up + 1 }, { col: at.col + 5, row: at.row + 5, up: at.up + 4 });
        await until(() => landing() === undefined, 2400, "the splash settled");
        const length = info().trackLength ?? 0;
        await until(() => (info().trainsAt ?? []).some((t) => t.station === null && t.front > length - 1 && t.speed > 1), 2400, "the next car at the end");
        await frames(8);
        await screenshot("slide_designs_flying");
        await until(() => landing() !== undefined, 600, "the car down off the end");
        await screenshot("slide_designs_landing");
        await frames(40);
        await screenshot("slide_designs_floating");
    }
    const crashes = info().crashes ?? 0;
    const moving = info().moving ?? 0;
    log("DESIGN", kind, design, "result pieces", info().pieces, "top speed", top.toFixed(1), "most lean", lean.toFixed(2),
        "crashes", crashes, "while tested", tested, "test", state ?? "ok", "open", refused ?? "ok", "moving", moving);
    if (info().pieces !== size)
        failed.push(`${design}: ${info().pieces} of ${size} pieces laid`);
    if (crashes > 0)
        failed.push(`${design}: ${crashes} crashes`);
    if (state != null)
        failed.push(`${design}: not tested (${state})`);
}
expect(failed.length === 0, `every design laid and run with no crash, not: ${failed.join("; ")}`);
