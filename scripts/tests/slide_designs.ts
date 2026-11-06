// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every saved design of a pool slide the game ships, laid on the blank landscape, tested and then opened: each is laid
// whole and runs several cars down with none crashing. Logs for each design its pieces, its cars' top speed, the most
// they swing up a trough's wall and any crash, and each train's place, speed and swing through its test.
//
//   ./nob script scripts/tests/slide_designs.ts -- --no-intro --no-audio --seed 1

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

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

const failed: string[] = [];
let shown: string | undefined;
for (const [kind, design, size] of kDesigns) {
    await park.open("Campaigns/Base/BlankLandscape.dat");
    await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "tools");
    expect(build.pick(`tracks/TrackedRides/${kind}/${kind}`, kind, "designs"), `a ${kind} to build`);
    await until(() => ui.find("design_browser") !== undefined, 600, "the designs window");
    await frames(3);
    const row = build.construction()!.designs.findIndex((d) => d.includes(design));
    expect(row >= 0, `the ${design} design among ${build.construction()!.designs.join(", ")}`);
    const list = ui.find("design_browser/content/list")!;
    const button = ui.find("design_browser/content/build")!;
    const unit = (button.bottom - button.top) / 22;
    // Under the column heads and the custom track's row, which stays first.
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
    if (spot === undefined) {
        log("DESIGN", kind, design, "no clear ground");
        failed.push(`${design}: no clear ground`);
        build.stop();
        continue;
    }
    await pointer.click(...spot);
    await frames(30);
    build.stop();
    await frames(10);
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
