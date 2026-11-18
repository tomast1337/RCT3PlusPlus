// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every saved design the install ships, each on a fresh blank landscape: picked from its kind's designs window,
// laid in the middle of the map (with pool water across its pool sides first, for a slide that needs it; with a
// paved square and a changing room beside its station after, for a lazy river, which opens only joined to a pool),
// tested with a time limit, and written as one row of designs.md and designs.json in the report: laid or refused and why,
// closed round or not, the test finished (or finished with a stall told on the way), crashed or stuck and where, its ratings and the seconds it took. A kind
// never tested is opened instead ("opens untested"), and one rated without a lap passes once the park runs it
// ("runs, no lap"). A screenshot only of a design that fails.
//
// The table is the result: the script passes unless it breaks itself, or with --strict unless every design builds
// and finishes its test. --range A-B runs designs A to B of the list only, counted from 0.
//
//   ./nob script scripts/tests/designs_smoke.ts -- --no-audio -- --range 0-49

export {};

/** Park seconds a test may run before it counts as unfinished. */
const kTestSeconds = 600;
/** Park seconds every train may stand still off its stations before it counts as stuck. */
const kStuckSeconds = 30;
/** Park seconds a ride with no lap to measure runs before its row is written. */
const kNoLapSeconds = 10;
const kPool = "Pool/Default/Default_Stub";
const kSteps: [number, number][] = [
    [0, 1],
    [1, 0],
    [0, -1],
    [-1, 0],
]; // By side, as the pools count them

interface Row {
    index: number;
    design: string;
    kind: string;
    laid: string; // "laid", or why not
    closed: string; // "yes", or why not
    test: string; // "finished", "finished, stalled", "crashed", "stuck", "unfinished", or why it did not run
    where: string;
    ratings: [number, number, number] | null;
    seconds: number;
    parkSeconds: number; // Of the test
}

const argv = args();
const strict = argv.includes("--strict");
const range = argv[argv.indexOf("--range") + 1]?.match(/^(\d+)-(\d+)$/);
if (argv.includes("--range")) expect(range != null, "--range written A-B");

async function blank(): Promise<void> {
    await park.open("Campaigns/Base/BlankLandscape.dat");
    // Some designs wait for a career scenario to be completed; with no career every one of them is tried.
    debug.use("unlock_designs");
    await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "tools");
}

await blank();
await until(() => build.designs() !== undefined, 1200, "the saved designs");
const all = build.designs()!;
expect(all.length > 0, "saved designs in the install");
const first = range ? Number(range[1]) : 0;
const last = range ? Math.min(Number(range[2]), all.length - 1) : all.length - 1;
log("designs", all.length, "running", first, "to", last);

/** Each design's row in its kind's window, sorted by name there: read from the window the first time. */
const rowOf = new Map<string, number>();

/** Opens the designs window of `kind`; whether it opened. */
async function openWindow(d: (typeof all)[number]): Promise<boolean> {
    if (!build.pick(d.overlay!, d.symbol, "designs")) return false;
    try {
        await until(() => ui.find("design_browser") !== undefined, 600, "the designs window");
    } catch {
        return false;
    }
    await frames(3);
    return true;
}

/** Clicks row `row` of the open designs window (0 the custom track's). */
async function clickRow(row: number): Promise<void> {
    const list = must(ui.find("design_browser/content/list"), "design_browser/content/list shown");
    const button = must(ui.find("design_browser/content/build"), "design_browser/content/build shown");
    const unit = (button.bottom - button.top) / 22; // A button is 22 of the window's units high
    // Under the column heads.
    await pointer.click(list.x, list.top + (18 + 16 * row + 8) * unit);
    await frames(3);
}

/** Picks design `d` in its window and builds it, leaving it in hand; why not, or "". */
async function pick(d: (typeof all)[number]): Promise<string> {
    if (!(await openWindow(d))) return "no designs window";
    const files = build.construction()!.designs;
    if (!rowOf.has(d.file))
        for (let row = 1; row <= files.length; ++row) {
            await clickRow(row);
            const shown = build.construction()?.shown;
            if (shown) rowOf.set(shown, row);
        }
    const row = rowOf.get(d.file);
    if (row === undefined) return "not in its window";
    // A second click on the row shown would be a double click, which builds it.
    if (build.construction()?.shown !== d.file) await clickRow(row);
    await ui.click("design_browser/content/build");
    await frames(10);
    if (ui.find("design_browser") !== undefined) {
        await ui.click("design_browser/content/cancel");
        return "not built from its window (locked)";
    }
    return "";
}

let aimed = { x: 0.5, y: 0.5 };
let trouble: MapPoint | undefined; // Where a failed test's train stopped, for its screenshot

/** Moves the pointer until the design in hand has its first piece on tile `goal`; whether it does. */
async function aim(goal: { col: number; row: number }): Promise<boolean> {
    let at = { ...goal };
    for (let i = 0; i < 4; ++i) {
        camera.look({ col: at.col, row: at.row }, { col: at.col, row: at.row + 30, up: 45 });
        await frames(4);
        aimed = must(map.screen(at.col, at.row), `tile ${at.col}, ${at.row} in sight`);
        pointer.move(aimed.x, aimed.y);
        await frames(4);
        const g = build.construction()?.ghostAt;
        if (!g) return false;
        if (g.col === goal.col && g.row === goal.row) return true;
        at = { col: at.col + goal.col - g.col, row: at.row + goal.row - g.row };
    }
    return false;
}

/** Opens the pool panel with `tool` of the pool list in hand. */
async function poolTool(tool: string): Promise<void> {
    const ridesTab = `tools/Rides/${must(ui.children("tools/Rides"), "tools/Rides shown")[0]}`;
    if (!ui.find("tools/Rides/Obj_Pools")) await ui.click(ridesTab);
    await until(() => ui.find("tools/Rides/Obj_Pools") !== undefined, 120, "the rides menu");
    await ui.click("tools/Rides/Obj_Pools");
    await until(() => ui.find("pool_panel/body/editor/Pool_PlaceWater") !== undefined, 120, "the pool panel");
    await frames(2); // The panel opens with the water in hand
    expect(build.pick(kPool, tool), `${tool} in the pool list`);
    await frames(2);
}

/** Lays the pool tool in hand on tile `t`. */
async function layPool(t: { col: number; row: number }): Promise<void> {
    camera.look({ col: t.col, row: t.row }, { col: t.col, row: t.row + 30, up: 45 });
    await frames(4);
    const p = must(map.screen(t.col, t.row), `tile ${t.col}, ${t.row} in sight`);
    await pointer.drag([p.x, p.y], [p.x, p.y], 4);
    await frames(3);
}

/** With a slide in hand at `at` where only its pool sides keep it from standing: water laid across them, then the
 * design picked and aimed again. Why it still may not stand, or "". */
async function byPool(d: (typeof all)[number], at: { col: number; row: number }): Promise<string> {
    const across = build.construction()!.poolSides.map((s) => ({
        col: s.col + kSteps[s.side][0],
        row: s.row + kSteps[s.side][1],
    }));
    await ui.click("design_placing/close");
    await frames(5);
    await poolTool("Pool_DepthNormal");
    for (const t of across) await layPool(t);
    build.stop();
    await frames(5);
    const again = await pick(d);
    if (again !== "") return again;
    if (!(await aim(at))) return "not aimed after the pool";
    return build.construction()?.refusal ?? "gone";
}

/** The track pieces on the tile under map point `p`, for where a train stopped. */
function piecesAt(p: { col: number; row: number }): string {
    const col = Math.floor(p.col);
    const row = Math.floor(p.row);
    const found = map
        .objects()
        .filter((o) => o.col === col && o.row === row && o.overlay.toLowerCase().startsWith("tracks/"))
        .map((o) => o.symbol);
    return `tile ${col},${row} ${[...new Set(found)].join("+") || "(no piece)"}`;
}

/** Builds and tests design `d`, filling its row. */
async function run(d: (typeof all)[number], row: Row): Promise<void> {
    if (d.overlay === null) {
        row.laid = "no ride of its kind";
        return;
    }
    await blank();
    const picked = await pick(d);
    if (picked !== "") {
        row.laid = picked;
        return;
    }
    const { width, height } = map.size();
    const middle = { col: Math.floor(width / 2), row: Math.floor(height / 2) };
    if (!(await aim(middle))) {
        row.laid = "no ghost in the middle";
        return;
    }
    let refusal: string = build.construction()!.refusal;
    if (refusal === "PlaceObject_IntersectsGround") {
        await ui.click("design_placing/TerrainFlattener_SmoothOff");
        await frames(3);
        refusal = build.construction()!.refusal;
    }
    if (refusal === "PlaceObject_MustEndOnPoolEdge") refusal = await byPool(d, middle);
    if (refusal !== "") {
        row.laid = `refused: ${refusal}`;
        return;
    }
    const ride = build.construction()!.ride;
    await pointer.click(aimed.x, aimed.y);
    await frames(30);
    const r = rides.list()[ride];
    if (r?.kind !== "tracked" || r.pieces === 0) {
        row.laid = "nothing laid on a click";
        return;
    }
    row.laid = build.construction() === undefined ? "laid" : "laid, building still open";
    if (build.construction() !== undefined) build.stop();
    await frames(5);

    let refused = rides.setState(ride, "testing");
    const untested = refused === "CannotTestTrackedRide";
    if (untested) refused = rides.setState(ride, "open");
    // A pool ride with no platform (a lazy river) opens once a station joins a pool complex: beside its first piece,
    // a station's, a paved square raised to the track, two units over the ground, then one on the ground and a
    // changing room, which makes the three a complex. Tried on each side in turn, the script not knowing which way
    // the station runs.
    if (refused?.startsWith("CantOpenPoolRide")) {
        await poolTool("Pool_PlaceTiles");
        for (const [dc, dr] of kSteps) {
            const out = (n: number) => ({ col: middle.col + dc * n, row: middle.row + dr * n });
            await layPool(out(2));
            const ground = map.pools().squares.find((q) => q.col === out(2).col && q.row === out(2).row)?.surface;
            if (ground === undefined) continue;
            await ui.click("pool_panel/body/editor/Pool_Height");
            await ui.click("pool_panel/body/editor/height/field");
            keys.press("Ctrl+A");
            keys.type(`${ground + 2}`);
            keys.press("Enter");
            await frames(2);
            await layPool(out(1));
            await ui.click("pool_panel/body/editor/Pool_Height");
            await ui.click("pool_panel/Pool_ChangingRoomPanel");
            await frames(2);
            await ui.click(`pool_panel/body/${must(ui.children("pool_panel/body"), "pool_panel/body shown")[0]}`);
            const p = must(map.screen(out(3).col + 0.5, out(3).row + 0.5), "the changing room's tile in sight");
            await pointer.click(p.x, p.y);
            await frames(5);
            refused = rides.setState(ride, "open");
            if (refused == null) break;
            await ui.click("pool_panel/Pool_PoolPanel");
            await frames(2);
            await ui.click("pool_panel/body/editor/Pool_PlaceTiles");
            await frames(2);
        }
        build.stop();
        await frames(5);
    }
    row.closed = refused ?? "yes";
    if (refused != null) {
        row.test = "not run";
        return;
    }
    await frames(2);
    const info = () => tracked(rides.list()[ride], "the ride");
    if (!info().joined) {
        row.test = "not run by the park";
        return;
    }
    if (untested || !info().measured) {
        await park.advance(kNoLapSeconds);
        row.parkSeconds = kNoLapSeconds;
        row.ratings = info().ratings ?? null;
        row.test = !info().joined ? "stopped by the park" : untested ? "opens untested" : "runs, no lap";
        return;
    }
    let still = 0;
    row.test = "unfinished";
    for (let t = 0; t < kTestSeconds; t += 1) {
        await park.advance(1);
        row.parkSeconds = t + 1;
        const now = info();
        row.ratings = now.ratings ?? null;
        if ((now.crashes ?? 0) > 0) {
            const wreck = now.wrecks?.[0];
            row.test = "crashed";
            row.where = wreck ? piecesAt(wreck) : "";
            trouble = wreck;
            return;
        }
        if (now.graphSamples > 0) {
            // A lap with a stall told on the way is no clean test.
            row.test = park.messages().some((m) => m.key === "RideTestStalled") ? "finished, stalled" : "finished";
            return;
        }
        const trains = now.trainsAt ?? [];
        const away = trains.findIndex((tr) => tr.station === null);
        still = trains.every((tr) => Math.abs(tr.speed) < 0.05) ? still + 1 : 0;
        if (still >= kStuckSeconds && trains.length > 0) {
            row.test = away < 0 ? "stuck in station" : "stuck";
            const t0 = away < 0 ? 0 : away;
            const car = now.cars[t0 * Math.max(1, now.carsPerTrain)];
            trouble = car;
            row.where = `${trains[t0].front.toFixed(1)} m of ${(now.trackLength ?? 0).toFixed(1)}${car ? `, ${piecesAt(car)}` : ""}`;
            return;
        }
    }
    const tr = info().trainsAt?.[0];
    if (tr) row.where = `${tr.front.toFixed(1)} m of ${(info().trackLength ?? 0).toFixed(1)}`;
    trouble = info().cars[0];
}

const rows: Row[] = [];
// Coasters whose test train stalls in our simulation, though they run in the original game: see #333. Titan's
// train leaves its last brakes too slow to climb the rise up to its station; Ravine's sled, let off its first
// block brake at the ride's speed, falls just short of the top of the climb to its second.
// With --strict, any other design that raises the stall message fails the run.
const kKnownStalls = ["Titan", "Ravine"];
const ok = (r: Row) =>
    r.laid === "laid" &&
    r.closed === "yes" &&
    (["finished", "opens untested", "runs, no lap"].includes(r.test) ||
        (r.test === "finished, stalled" && kKnownStalls.includes(r.design)));

function write(): void {
    const fmt = (v: number) => (v * 10).toFixed(2);
    const lines = [
        "| # | Design | Kind | Laid | Closed | Test | Park s | Where | E | I | N | s |",
        "|---|--------|------|------|--------|------|--------|-------|---|---|---|---|",
        ...rows.map(
            (r) =>
                `| ${r.index} | ${r.design} | ${r.kind} | ${r.laid} | ${r.closed} | ${r.test} | ${r.parkSeconds} | ${r.where} | ${r.ratings ? r.ratings.map(fmt).join(" | ") : " | | "} | ${r.seconds.toFixed(0)} |`,
        ),
        "",
        `${rows.filter(ok).length} of ${rows.length} built and tested (or opened, for a kind never tested).`,
    ];
    report("designs.md", lines.join("\n") + "\n");
    report("designs.json", JSON.stringify(rows, null, 1));
}

for (let i = first; i <= last; ++i) {
    const d = all[i];
    const row: Row = {
        index: i,
        design: d.file.replace(/^\[[^\]]*\]/, "").replace(/\.trk$/i, ""),
        kind: d.symbol,
        laid: "",
        closed: "",
        test: "",
        where: "",
        ratings: null,
        seconds: 0,
        parkSeconds: 0,
    };
    const since = Date.now();
    trouble = undefined;
    try {
        await run(d, row);
    } catch (e) {
        row.test = `script error: ${e instanceof Error ? e.message : String(e)}`;
    }
    row.seconds = (Date.now() - since) / 1000;
    rows.push(row);
    log("DESIGN", JSON.stringify(row));
    if (!ok(row)) {
        const at = trouble as MapPoint | undefined; // Set inside run(), which the compiler does not follow
        if (at) {
            camera.look(at, { col: at.col + 6, row: at.row + 6, up: (at.up ?? 0) + 8 });
            await frames(10);
        }
        await screenshot(`fail_${i}_${row.kind}`);
    }
    write();
}
write();
expect(!strict || rows.every(ok), `every design built and tested, not ${rows.filter((r) => !ok(r)).length} of them`);
