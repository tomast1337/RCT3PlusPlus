// SPDX-License-Identifier: GPL-3.0-or-later
//
// A pool complex with a water slide: swimmers go up to its station, ride it down in their swimwear and splash
// into the pool, where rings spread on the water, and they swim on. A car running off the slide's open end into the water is no crash: no
// wreck, no message, the slide stays open, rated, and its cars go round again. Give it a park the game saved
// with a body, ring or pro slide ending in a pool and guests swimming:
//
//   ./nob script scripts/tests/pool_slides.ts -- --no-intro --no-audio --seed 1 --park PARK

export {};

await until(() => park.loaded(), 1200, "the park");
await frames(10);
park.setWeather("sunny");
park.setTemperature(28);
park.setTime(0.5);
const slide = rides.list().findIndex((r) => !r.flat && !r.demolished && /(body|ring|pro)slide/i.test(r.symbol));
expect(slide >= 0, "a pool slide in the park");
const info = () => rides.list()[slide];
log("slide", info().name, info().symbol, "trains", info().trains, "length", info().trackLength?.toFixed(1));
expect(info().joined, "the slide run by the park");
expect(info().ratings !== undefined, "the slide rated");
const messages = park.messages().length;

// A swimmer riding it, in their swimwear: a look at them on the way down.
const riding = () => people.list().findIndex((p) => p.state === "riding" && p.ride === slide);
await until(
    () => riding() >= 0 && (info().trainsAt ?? []).some((t) => t.station === null && t.riders > 0),
    6000,
    "a swimmer riding the slide",
);
await until(() => riding() >= 0 && people.list()[riding()].reaction !== "waiting", 600, "the rider on their way down");
const rider = riding();
expect(people.list()[rider].swimwear === true, "the rider in their swimwear");
const at = people.list()[rider];
camera.look({ col: at.col!, row: at.row!, up: at.up! }, { col: at.col! + 3, row: at.row! + 3, up: at.up! + 3 });
await frames(6);
await screenshot("pool_slides_riding");

// Off the end they splash down into the pool, a low burst of spray and a ring of foam where the slide meets the
// water, and swim on, still in their swimwear.
await until(() => people.list()[rider].state !== "riding", 2400, "the rider off the end");
const flying = people.list()[rider];
log("off the end as", flying.state, "swimwear", flying.swimwear);
expect(flying.swimwear === true, "the rider still in their swimwear off the end");
const splashing = () =>
    map
        .effects()
        .filter(
            (e) =>
                /^(splash2|waterwakemedium)$/.test(e.effect) &&
                Math.abs(e.col - flying.col!) + Math.abs(e.row - flying.row!) < 3,
        );
await until(
    () => new Set(splashing().map((e) => e.effect)).size === 2,
    300,
    "spray and foam where the rider came down",
);
const splashes = splashing();
const rings = map.rings().filter((r) => Math.abs(r.col - splashes[0].col) + Math.abs(r.row - splashes[0].row) < 0.5);
expect(
    rings.length === 1 && rings[0].crest > 0,
    `rings spreading where the rider came down, not ${JSON.stringify(map.rings())}`,
);
await until(() => people.list()[rider].state === "swimming", 600, "the rider swimming again");
expect(people.list()[rider].swimwear === true, "the rider swimming in their swimwear");
await frames(30);
await screenshot("pool_slides_swimming");

// A close look from the side at where the riders come down, the slide's end beyond, as the next car goes off it:
// mid-splash with its rider, then the foam spreading.
const mouth = splashes[0];
const flew = { col: mouth.col - flying.col!, row: mouth.row - flying.row! };
const far = Math.hypot(flew.col, flew.row) || 1;
camera.look(mouth, {
    col: mouth.col - (flew.row / far) * 1.2 + (flew.col / far) * 0.6,
    row: mouth.row + (flew.col / far) * 1.2 + (flew.row / far) * 0.6,
    up: mouth.up + 0.7,
});
const near_end = () =>
    map
        .effects()
        .filter((e) => e.effect === "waterwakemedium" && Math.abs(e.col - mouth.col) + Math.abs(e.row - mouth.row) < 1);
await until(() => near_end().length === 0, 1200, "the spray settled");
await until(() => near_end().length > 0, 6000, "the next car off the end");
await frames(15);
await screenshot("pool_slides_splash_mid");
await frames(45);
await screenshot("pool_slides_splashdown");

// Cars go off the end again and again, and none of it is a crash.
await until(() => (info().customers ?? 0) >= 3, 6000, "three swimmers down the slide");
expect((info().crashes ?? 0) === 0, `no crash, not ${info().crashes}`);
expect((info().wrecks ?? []).length === 0, "no wrecks");
expect(info().open === true, "the slide still open");
const told = park
    .messages()
    .slice(messages)
    .map((m) => m.key);
expect(!told.some((k) => /crash/i.test(k)), `no crash message, not ${told.join(" ")}`);
// Swimmers get on from the pool, so there is no entrance on the paths to complain of.
expect(
    !park
        .messages()
        .slice(messages)
        .some((m) => /NoPathToEntrance/.test(m.key) && m.name === info().name),
    "no complaint about the slide's entrance",
);
const people_now = people.list();
expect(
    people_now.every((p) => p.state !== "thrown"),
    "nobody thrown",
);
log("customers", info().customers, "riders now", info().riders);

// A pool path trail starts from the slide's platform: pinned, from a side of a platform square off the slide's
// track, at the platform's height, the first piece's ghost and arrow out from it; laid, the piece is level with
// the platform.
const ride = info();
expect((ride.platformSquares ?? []).length > 0, "the slide's platform squares");
log("platform squares", JSON.stringify(ride.platformSquares));
const rides_menu = `tools/Rides/${must(ui.children("tools/Rides"), "tools/Rides shown")[0]}`;
await until(() => build.flatRides() !== undefined, 1200, "the build items");
if (!ui.find("tools/Rides/Obj_Pools")) await ui.click(rides_menu);
await until(() => ui.find("tools/Rides/Obj_Pools") !== undefined, 120, "the rides menu");
await ui.click("tools/Rides/Obj_Pools");
await until(() => ui.find("pool_panel/Pool_PathPanel") !== undefined, 120, "the pool panel");
await ui.click("pool_panel/Pool_PathPanel");
await until(() => ui.find("pool_panel/body/paths") !== undefined, 120, "the paths page, pinned");
await ui.click("pool_panel/body/paths/Path_NoSlope");
const kOut = [
    [0, 1],
    [1, 0],
    [0, -1],
    [-1, 0],
];
let anchored: { col: number; row: number; height: number; out: number[] } | undefined;
for (const square of ride.platformSquares!) {
    if (anchored) break;
    for (const turn of [square.turn & 3, (square.turn + 2) & 3]) {
        const out = kOut[turn];
        const beside = [square.col + out[0], square.row + out[1]];
        if (
            map
                .objects()
                .some(
                    (o) =>
                        o.overlay === "Pool/Default/Default_Stub" &&
                        o.col === beside[0] &&
                        o.row === beside[1] &&
                        o.height === square.height,
                )
        )
            continue;
        const up = square.height - Math.max(...map.tile(square.col, square.row)!.corners);
        camera.look(
            { col: square.col + 0.5, row: square.row + 0.5, up },
            { col: square.col + 0.5 + out[0] * 3, row: square.row + 0.5 + out[1] * 3, up: up + 40 },
        );
        await frames(10);
        const at = map.screen(square.col + 0.5 + out[0] * 0.4, square.row + 0.5 + out[1] * 0.4, square.height);
        if (!at) {
            log("out of sight", JSON.stringify(square), "side", turn);
            continue;
        }
        pointer.move(at.x, at.y);
        await frames(3);
        const shown = build.pooling();
        log("at the platform", JSON.stringify(square), "side", turn, JSON.stringify(shown));
        if (
            shown?.ghost?.startsWith("Path") &&
            shown.ghostAt?.col === beside[0] &&
            shown.ghostAt?.row === beside[1] &&
            shown.ghostAt?.height === square.height &&
            shown.arrow
        ) {
            anchored = { col: beside[0], row: beside[1], height: square.height, out };
            await screenshot("pool_slides_path_ghost");
            await pointer.click(at.x, at.y);
            await frames(2);
            break;
        }
    }
}
expect(anchored !== undefined, "a trail started at the slide's platform, its ghost and arrow shown");
await ui.click("pool_panel/body/paths/Path_Straight");
await ui.click("pool_panel/body/paths/Path_Straight");
await frames(5);
const joined = map
    .objects()
    .find(
        (o) =>
            o.overlay === "Pool/Default/Default_Stub" &&
            o.col === anchored!.col &&
            o.row === anchored!.row &&
            o.symbol.startsWith("Path"),
    );
expect(
    joined !== undefined && joined.height === anchored!.height,
    `a pool path laid level with the platform, not ${JSON.stringify(joined)}`,
);
build.stop();
await frames(10);
await screenshot("pool_slides_path_joined");
