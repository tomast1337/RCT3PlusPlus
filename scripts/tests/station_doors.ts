// SPDX-License-Identifier: GPL-3.0-or-later
//
// Builds a coaster of two stations on the game's blank landscape: its first station, two straights, a second
// station, a straight, then auto-complete closes the circuit. Each station takes its own entrance and exit, placed
// beside it from the ride's window, and a path behind each door joins them to the park. Its trains stop at both
// stations, doors or not: two trains stand one at each, and tested they stop at the second before it has doors,
// their cars drawn moving smoothly through the stations. Opened with doors at both, a train stands at each. Saved
// and loaded again, both stations keep their doors and the platforms their pieces stand in.
//
//   ./nob script scripts/tests/station_doors.ts -- --no-audio

export {};

type Tile = { col: number; row: number };

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "tools");
expect(build.pick("tracks/coasters/Corkscrew/Corkscrew", "Corkscrew", "designs"), "a corkscrew coaster to build");
await until(() => ui.find("design_browser") !== undefined, 600, "the designs window");
await frames(3);
// The custom track, the window's first row.
await ui.click("design_browser/content/build");
await frames(10);
const ride = build.construction()!.ride;
const { width, height } = map.size();
const centre = { col: Math.floor(width / 2), row: Math.floor(height / 2) };
camera.look(
    { col: centre.col + 0.5, row: centre.row + 0.5 },
    { col: centre.col + 0.5, row: centre.row + 14.5, up: 30 },
);
await frames(5);
/** Picks the station among the pictured pieces: its button, clicked again to build it. */
async function pickStation(): Promise<string | undefined> {
    for (let i = 0; i < 8; ++i) {
        const special = `placing/body/construction/special${i}`;
        if (ui.find(special) === undefined) continue;
        await ui.click(special);
        await frames(10);
        if ((build.construction()?.piece ?? "").toLowerCase().includes("station")) return special;
    }
    return undefined;
}
/** Lays a straight, clicked until one more piece is laid. */
async function straight(): Promise<void> {
    const before = rides.list()[ride].pieces ?? 0;
    for (let i = 0; i < 3 && (rides.list()[ride].pieces ?? 0) === before; ++i) {
        await ui.click("placing/body/construction/AttractionEditor_IconCurve0");
        await frames(10);
    }
    expect((rides.list()[ride].pieces ?? 0) === before + 1, "a straight laid");
}

const at = must(map.screen(centre.col, centre.row), `tile ${centre.col}, ${centre.row} in sight`);
pointer.move(at.x, at.y);
await frames(10);
expect((await pickStation()) !== undefined, "a station piece to start with");
await frames(30);
const first: Tile = build.construction()!.ghostAt!;
await pointer.click(at.x, at.y);
await frames(30);
expect((rides.list()[ride].pieces ?? 0) === 1, "the first station laid");
await straight();
await straight();
const special = await pickStation();
expect(special !== undefined, "a station piece to build");
const second: Tile = build.construction()!.ghostAt!;
await ui.click(special!);
await frames(10);
expect(rides.list()[ride].pieces === 4, "the second station laid");
await straight();
log("track", JSON.stringify(rides.list()[ride].track));

await ui.click("placing/body/construction/complete");
await until(
    () => build.construction()?.completing === false && (build.construction()?.proposed ?? 0) > 0,
    600,
    "a way round to the first station",
);
await ui.click("placing/body/construction/complete");
await frames(10);
log("closed", JSON.stringify(rides.list()[ride].track));

/** Places the door at the station on `station` from the ride's window: tried on the tiles round it. */
async function placeDoor(station: Tile, step: "entrance" | "exit"): Promise<void> {
    if (build.placing()?.step !== step) {
        rides.show(ride);
        await frames(5);
        await ui.click("info/Guest_Rides");
        await frames(3);
        await ui.click(`info/body/Attraction_General_${step === "entrance" ? "Entrance" : "Exit"}`);
        await frames(3);
    }
    expect(build.placing()?.step === step, `placing the ${step}, not ${JSON.stringify(build.placing())}`);
    camera.look(
        { col: station.col + 0.5, row: station.row + 0.5 },
        { col: station.col + 0.5, row: station.row + 10.5, up: 20 },
    );
    await frames(5);
    for (const [dc, dr] of [
        [0, 1],
        [0, -1],
        [1, 0],
        [-1, 0],
    ]) {
        const p = map.screen(station.col + dc, station.row + dr);
        if (p === undefined) continue;
        pointer.move(p.x, p.y);
        await frames(3);
        if (build.placing()?.problem === "none") {
            await pointer.click(p.x, p.y);
            await frames(5);
            return;
        }
    }
    throw new Error(`no tile beside ${JSON.stringify(station)} for the ${step}`);
}

/** Clicks the up or down arrow of the number box `key` on the vehicles tab of the ride's window. */
async function step(key: string, up: boolean): Promise<void> {
    rides.show(ride);
    await frames(5);
    await ui.click("info/Attraction_Vehicles");
    await frames(5);
    const box = ui.find(`info/body/${key}`);
    expect(box !== undefined, `the ${key} box`);
    const quarter = (box!.bottom - box!.top) / 4;
    await pointer.click(box!.right - 0.002, up ? box!.top + quarter : box!.bottom - quarter);
    await frames(10);
}

/** Watches the ride for `count` frames: frames a train stood at each station, and the most a car's drawn step
 * changed from one frame to the next, or turned back. */
async function watch(count: number): Promise<{ stood: number[]; change: number; back: number }> {
    const stood = [0, 0];
    let change = 0;
    let back = 0;
    let last: { col: number; row: number }[] = [];
    let steps: { dc: number; dr: number }[] = [];
    for (let n = 0; n < count; ++n) {
        await frames(1);
        const r = rides.list()[ride];
        for (const t of r.trainsAt ?? []) if (t.station !== null && t.station < stood.length) ++stood[t.station];
        const cars = r.cars ?? [];
        const now = cars.map((c, k) => ({ dc: c.col - (last[k]?.col ?? c.col), dr: c.row - (last[k]?.row ?? c.row) }));
        if (last.length === cars.length && steps.length === cars.length)
            for (let k = 0; k < cars.length; ++k) {
                const a = steps[k];
                const b = now[k];
                change = Math.max(change, Math.hypot(b.dc - a.dc, b.dr - a.dr));
                // Turned back: a step against the one before, both more than a hundredth of a tile.
                if (Math.hypot(a.dc, a.dr) > 0.01 && Math.hypot(b.dc, b.dr) > 0.01 && a.dc * b.dc + a.dr * b.dr < 0)
                    ++back;
            }
        last = cars.map((c) => ({ col: c.col, row: c.row }));
        steps = now;
    }
    return { stood, change, back };
}

const doors = () => rides.list()[ride].stations;
/** Lays a path behind each door of station `k`, joining it to the park. */
async function pathsBehind(k: number): Promise<void> {
    await ui.click("tools/Paths/Obj_Paths");
    await until(() => ui.find("footpaths/route") !== undefined, 120, "the path panel");
    await ui.toggle("footpaths/follow", false);
    const behind = [
        [1, 0],
        [0, -1],
        [-1, 0],
        [0, 1],
    ];
    for (const door of [doors()[k].entrance!, doors()[k].exit!]) {
        const [dc, dr] = behind[door.direction & 3];
        const col = door.col + dc;
        const row = door.row + dr;
        camera.look({ col: col + 0.5, row: row + 0.5 }, { col: col + 0.5, row: row + 10.5, up: 30 });
        await frames(5);
        const p = must(map.screen(col, row), `tile ${col}, ${row} in sight`);
        pointer.move(p.x, p.y);
        await frames(10);
        await pointer.click(p.x, p.y);
        await frames(30);
    }
    build.stop();
}

// The first station's doors, on the paths: the park runs the ride, stopping at the second station too.
await placeDoor(first, "entrance");
await placeDoor(first, "exit");
await pathsBehind(0);
await until(() => rides.list()[ride].joined, 300, "the ride run by the park");
expect(
    rides.list()[ride].stops === 1,
    `a stop at the second station before its doors, not ${rides.list()[ride].stops}`,
);
// Two trains: one at each station, apart.
await step("AttractionVehicleDialogLabelNumTrains", true);
const spread = rides.list()[ride].trainsAt ?? [];
log("two trains", JSON.stringify(spread), "track", rides.list()[ride].trackLength);
expect(spread.length === 2, `two trains, not ${spread.length}`);
expect(
    spread
        .map((t) => t.station)
        .sort()
        .join() === "0,1",
    `a train standing at each station, not ${JSON.stringify(spread)}`,
);
const whole = rides.list()[ride].trackLength!;
const apart = (((spread[0].front - spread[1].front) % whole) + whole) % whole;
expect(
    Math.min(apart, whole - apart) > Math.max(spread[0].length, spread[1].length),
    `the trains apart, not ${apart.toFixed(1)} m`,
);
camera.look(
    { col: (first.col + second.col) / 2 + 0.5, row: (first.row + second.row) / 2 + 0.5 },
    { col: (first.col + second.col) / 2 + 0.5, row: (first.row + second.row) / 2 + 16.5, up: 22 },
);
await frames(10);
await screenshot("two_trains");
// Tested, the trains stop at both stations, the second without doors, and are drawn moving on smoothly.
expect(rides.setState(ride, "testing") == null, "the ride tested");
const tested = await watch(60 * 60);
log("tested", JSON.stringify(tested));
expect(
    tested.stood[0] > 0 && tested.stood[1] > 0,
    `trains stopped at both stations while tested, the second without doors, not ${JSON.stringify(tested.stood)}`,
);
expect(tested.back === 0, `no car drawn turning back, not ${tested.back} times`);
expect(
    tested.change < 0.02,
    `no car's drawn step changing by more than 0.02 of a tile a frame, not ${tested.change.toFixed(3)}`,
);
// The second station's doors, placed once it is closed again (its window offers doors only then): the park keeps
// running it as it is, its guests coming once a path reaches them.
expect(rides.setState(ride, "closed") == null, "the ride closed");
await frames(5);
await placeDoor(second, "entrance");
await placeDoor(second, "exit");
build.stop();
log("stations", JSON.stringify(doors()));
expect(
    doors().length === 2 && doors().every((s) => s.entrance !== null && s.exit !== null),
    `an entrance and an exit at both stations, not ${JSON.stringify(doors())}`,
);
expect(rides.list()[ride].stops === 1, "still a stop at the second station");
await pathsBehind(1);
// Open, a train stands at each station for its riders.
expect(rides.setState(ride, "open") == null, `the ride opened, not refused with ${rides.setState(ride, "open")}`);
await park.advanceUntil(
    () =>
        (rides.list()[ride].trainsAt ?? [])
            .map((t) => t.station)
            .sort()
            .join() === "0,1",
    120,
    "a train standing at each station once open",
);
camera.look(
    { col: (first.col + second.col) / 2 + 0.5, row: (first.row + second.row) / 2 + 0.5 },
    { col: (first.col + second.col) / 2 + 0.5, row: (first.row + second.row) / 2 + 16.5, up: 22 },
);
await frames(30);
await screenshot("two_stations");

// Saved and loaded again.
const saved = JSON.stringify(doors());
// Each station piece of both stations stands in its platform, with stairs beside the doors.
const platforms = rides.list()[ride].platforms ?? [];
log("platforms", JSON.stringify(platforms));
expect(
    platforms.length === 2 && platforms.every((p) => /stair/i.test(p)),
    `a platform with stairs on both station pieces, not ${JSON.stringify(platforms)}`,
);
await ui.click(`tools/Files/${must(ui.children("tools/Files"), "tools/Files shown")[0]}`);
await ui.click("tools/Files/File_SaveGame");
await until(() => ui.find("park_files") !== undefined, 120, "the save window");
await ui.click("park_files/content/open");
await until(() => ui.find("park_files") === undefined, 120, "the save window to close");
if (!ui.find("tools/Files/File_LoadGame"))
    await ui.click(`tools/Files/${must(ui.children("tools/Files"), "tools/Files shown")[0]}`);
await until(() => ui.find("tools/Files/File_LoadGame") !== undefined, 120, "the files menu");
await ui.click("tools/Files/File_LoadGame");
// Loading asks first whether to save; the park was saved just now.
await until(() => ui.find("load_question") !== undefined, 120, "the question before loading");
await ui.click("load_question/content/answer1");
const row = "park_files/content/list/view/content/row0";
await until(() => ui.find(row) !== undefined, 120, "the save in the load window");
await ui.click(row);
await ui.click("park_files/content/open");
await until(() => park.loaded() && rides.list().length > ride, 1200, "the park back after loading");
await until(
    () => rides.list()[ride].joined && (rides.list()[ride].stops ?? 0) > 0,
    600,
    "the second station stopped at after loading",
);
expect(JSON.stringify(doors()) === saved, `both stations' doors back after loading, not ${JSON.stringify(doors())}`);
expect(
    JSON.stringify(rides.list()[ride].platforms) === JSON.stringify(platforms),
    `both stations' platforms back after loading, not ${JSON.stringify(rides.list()[ride].platforms)}`,
);
camera.look(
    { col: (first.col + second.col) / 2 + 0.5, row: (first.row + second.row) / 2 + 0.5 },
    { col: (first.col + second.col) / 2 + 0.5, row: (first.row + second.row) / 2 + 16.5, up: 22 },
);
await frames(30);
await screenshot("two_stations_loaded");
