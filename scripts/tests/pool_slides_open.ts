// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every pool slide and lazy river a park was saved with opens again once closed: a ring slide's or water cannon's
// station track stands over the deck of the paving, pool path or stairs beside its platform, and still joins it.
// Swimmers then ride each slide not broken down. A ring slide laid from a saved design, its end in a pool of its own, is refused
// till a pool path laid off its platform from its window's join button joins it, then opens. Give it a park the
// game saved with pool slides open:
//
//   ./nob script scripts/tests/pool_slides_open.ts -- --no-intro --no-audio --seed 1 --park PARK

export {};

await until(() => park.loaded(), 1200, "the park");
await frames(10);
park.setWeather("sunny");
park.setTemperature(28);
park.setTime(0.5);
const kPool = "Pool/Default/Default_Stub";
const kSteps: [number, number][] = [
    [0, 1],
    [1, 0],
    [0, -1],
    [-1, 0],
]; // By side, as the pools count them
const pool_ride = (symbol: string) => /^(BodySlide|ProSlide|RingSlide|MasterBlaster|LazyRiver):/i.test(symbol);

// 1. Each saved pool ride, closed and opened again, opens.
const saved = rides.list().flatMap((r, i) => (!r.demolished && pool_ride(r.symbol) ? [i] : []));
expect(saved.length > 0, "a pool ride in the park");
for (const i of saved) {
    const r = rides.list()[i];
    const closing = rides.setState(i, "closed");
    const why = rides.setState(i, "open");
    log("reopened", r.name, r.symbol, "closing:", closing ?? "yes", "open:", why ?? "yes");
    expect(why == null, `${r.name} (${r.symbol}) open again, not ${why}`);
}

// 2. Swimmers ride each slide again, but one broken down, which stays shut till mended.
for (const slide of saved.filter((i) => !/LazyRiver/i.test(rides.list()[i].symbol))) {
    const info = () => rides.list()[slide];
    if (info().open !== true) {
        log("not ridden, broken down:", info().name);
        continue;
    }
    await until(
        () =>
            people.list().some((p) => p.state === "riding" && p.ride === slide) &&
            (info().trainsAt ?? []).some((t) => t.station === null && t.riders > 0),
        6000,
        `a swimmer riding ${info().name}`,
    );
    const rider = must(
        people.list().find((p) => p.state === "riding" && p.ride === slide),
        "a rider on the slide",
    );
    camera.look(
        { col: rider.col!, row: rider.row!, up: rider.up! },
        { col: rider.col! + 9, row: rider.row! + 9, up: rider.up! + 9 },
    );
    await frames(10);
    await screenshot(`pool_slides_open_riding_${slide}`);
}

// 3. A ring slide laid from a saved design, in a pool of its own where its end needs one.
let here: [number, number] = [0, 0]; // Where the pointer was last moved for a ghost
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
await until(() => build.flatRides() !== undefined, 1200, "the build lists");
await pick();
camera.look({ col: 20, row: 20, up: 0 }, { col: 20, row: 50, up: 60 });
await frames(10);
let spot = false;
for (let k = 0; k < 60 && !spot; ++k) {
    here = [0.05 + 0.06 * (k % 15), 0.3 + 0.1 * Math.floor(k / 15)];
    pointer.move(...here);
    await frames(3);
    const c = build.construction();
    spot = c !== undefined && c.ghost && (c.refusal === "" || c.refusal === "PlaceObject_MustEndOnPoolEdge");
}
if (spot && build.construction()!.refusal !== "") {
    // Water laid with the pool tool across its open end, then the design picked again, turned and aimed as before.
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
    spot = (await aim(at)) && build.construction()?.refusal === "";
}
expect(spot, "clear ground for the design");
await pointer.click(...here);
await frames(30);
const built = rides.list().length - 1;
expect(/ringslide/i.test(rides.list()[built].symbol) && rides.list()[built].pieces! > 1, "the slide laid");
build.stop();
await frames(5);
const refused = rides.setState(built, "open");
expect(refused === "CantOpenPoolRideNoEntrance", `the new slide refused till joined to a pool, not ${refused}`);

// Its window's join button opens the pool paths page; pinned, a press off the platform's side lays a path there at
// its deck, joining the station.
const deck = rides.list()[built].platformSquares![1] ?? rides.list()[built].platformSquares![0];
log("new slide", rides.list()[built].name, "platform", JSON.stringify(rides.list()[built].platformSquares));
rides.show(built);
await frames(10);
await ui.click("info/body/Pool_PathPanel");
await until(() => ui.find("pool_panel/Pool_PathPanel")?.on === true, 120, "the pool panel on its paths page");
await ui.toggle("pool_panel/pin", true);
await ui.click("pool_panel/body/paths/Path_NoSlope");
let laid = false;
for (const side of [deck.turn, (deck.turn + 2) % 4]) {
    const [dc, dr] = kSteps[side];
    camera.look(
        { col: deck.col - 5 * dc, row: deck.row - 5 * dr },
        { col: deck.col + 5 * dc + 2 * dr, row: deck.row + 5 * dr + 2 * dc, up: deck.height + 14 },
    );
    await frames(20);
    const at = map.screen(deck.col + 0.5 + 0.35 * dc, deck.row + 0.5 + 0.35 * dr, deck.height);
    if (!at) continue;
    pointer.move(at.x, at.y);
    await frames(3);
    const g = build.pooling()?.ghostAt;
    log("side", side, "ghost", JSON.stringify(build.pooling()));
    if (g?.col !== deck.col + dc || g?.row !== deck.row + dr || g?.height !== deck.height) continue;
    await pointer.click(at.x, at.y);
    await frames(3);
    await ui.click("pool_panel/body/paths/Path_Straight");
    await frames(5);
    laid = map
        .objects()
        .some(
            (o) =>
                o.overlay === kPool &&
                o.symbol.startsWith("Path") &&
                o.col === deck.col + dc &&
                o.row === deck.row + dr &&
                o.height === deck.height,
        );
    if (laid) break;
}
expect(laid, "a pool path laid off the new slide's platform at its deck");
build.stop();
await frames(10);
const why = rides.setState(built, "open");
log("new slide joined, open:", why ?? "yes");
expect(why == null, `the joined new slide open, not ${why}`);
await screenshot("pool_slides_open_joined");
