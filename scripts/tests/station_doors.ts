// SPDX-License-Identifier: GPL-3.0-or-later
//
// Builds a coaster of two stations on the game's blank landscape: its first station, two straights, a second
// station, a straight, then auto-complete closes the circuit. Each station takes its own entrance and exit, placed
// beside it from the ride's window; once the second has its doors the park's trains stop there too, and a path
// behind each door joins them to the park. Saved and loaded again, both stations keep their doors.
//
//   ./nob script scripts/tests/station_doors.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

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
camera.look({ col: centre.col + 0.5, row: centre.row + 0.5 }, { col: centre.col + 0.5, row: centre.row + 14.5, up: 30 });
await frames(5);
/** Picks the station among the pictured pieces: its button, clicked again to build it. */
async function pickStation(): Promise<string | undefined> {
    for (let i = 0; i < 8; ++i) {
        const special = `placing/body/construction/special${i}`;
        if (ui.find(special) === undefined)
            continue;
        await ui.click(special);
        await frames(10);
        if ((build.construction()?.piece ?? "").toLowerCase().includes("station"))
            return special;
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

const at = map.screen(centre.col, centre.row)!;
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
await until(() => build.construction()?.completing === false && (build.construction()?.proposed ?? 0) > 0, 600, "a way round to the first station");
await ui.click("placing/body/construction/complete");
await frames(10);
log("closed", JSON.stringify(rides.list()[ride].track));

/** Places the door at the station on `station` from the ride's window: tried on the tiles round it. */
async function placeDoor(station: Tile, step: "entrance" | "exit"): Promise<void> {
    if (build.placing()?.step !== step) {
        rides.show(ride);
        await frames(5);
        await ui.click(`info/body/Attraction_General_${step === "entrance" ? "Entrance" : "Exit"}`);
        await frames(3);
    }
    expect(build.placing()?.step === step, `placing the ${step}, not ${JSON.stringify(build.placing())}`);
    camera.look({ col: station.col + 0.5, row: station.row + 0.5 }, { col: station.col + 0.5, row: station.row + 10.5, up: 20 });
    await frames(5);
    for (const [dc, dr] of [[0, 1], [0, -1], [1, 0], [-1, 0]]) {
        const p = map.screen(station.col + dc, station.row + dr);
        if (p === undefined)
            continue;
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

const doors = () => rides.list()[ride].stations;
/** Lays a path behind each door of station `k`, joining it to the park. */
async function pathsBehind(k: number): Promise<void> {
    await ui.click("tools/Paths/Obj_Paths");
    await until(() => ui.find("footpaths/route") !== undefined, 120, "the path panel");
    await ui.toggle("footpaths/follow", false);
    const behind = [[1, 0], [0, -1], [-1, 0], [0, 1]];
    for (const door of [doors()[k].entrance!, doors()[k].exit!]) {
        const [dc, dr] = behind[door.direction & 3];
        const col = door.col + dc;
        const row = door.row + dr;
        camera.look({ col: col + 0.5, row: row + 0.5 }, { col: col + 0.5, row: row + 10.5, up: 30 });
        await frames(5);
        const p = map.screen(col, row)!;
        pointer.move(p.x, p.y);
        await frames(10);
        await pointer.click(p.x, p.y);
        await frames(30);
    }
    build.stop();
}

// The first station's doors, on the paths: the park runs the ride, stopping nowhere else.
await placeDoor(first, "entrance");
await placeDoor(first, "exit");
await pathsBehind(0);
await until(() => rides.list()[ride].joined, 300, "the ride run by the park");
expect(rides.list()[ride].stops === 0, `no second stop before its doors, not ${rides.list()[ride].stops}`);
// The second station's: the closed ride is taken on again, its trains stopping there too, its guests coming once a
// path reaches the doors.
await placeDoor(second, "entrance");
await placeDoor(second, "exit");
build.stop();
log("stations", JSON.stringify(doors()));
expect(doors().length === 2 && doors().every((s) => s.entrance !== null && s.exit !== null), `an entrance and an exit at both stations, not ${JSON.stringify(doors())}`);
await until(() => rides.list()[ride].stops === 1, 300, "the trains stopping at the second station");
await pathsBehind(1);
camera.look({ col: (first.col + second.col) / 2 + 0.5, row: (first.row + second.row) / 2 + 0.5 }, { col: (first.col + second.col) / 2 + 0.5, row: (first.row + second.row) / 2 + 16.5, up: 22 });
await frames(30);
await screenshot("two_stations");

// Saved and loaded again.
const saved = JSON.stringify(doors());
await ui.click(`tools/Files/${ui.children("tools/Files")![0]}`);
await ui.click("tools/Files/File_SaveGame");
await until(() => ui.find("park_files") !== undefined, 120, "the save window");
await ui.click("park_files/content/open");
await until(() => ui.find("park_files") === undefined, 120, "the save window to close");
if (!ui.find("tools/Files/File_LoadGame"))
    await ui.click(`tools/Files/${ui.children("tools/Files")![0]}`);
await until(() => ui.find("tools/Files/File_LoadGame") !== undefined, 120, "the files menu");
await ui.click("tools/Files/File_LoadGame");
const row = "park_files/content/list/view/content/row0";
await until(() => ui.find(row) !== undefined, 120, "the save in the load window");
await ui.click(row);
await ui.click("park_files/content/open");
await until(() => park.loaded() && rides.list().length > ride, 1200, "the park back after loading");
await until(() => rides.list()[ride].joined && (rides.list()[ride].stops ?? 0) > 0, 600, "the second station stopped at after loading");
expect(JSON.stringify(doors()) === saved, `both stations' doors back after loading, not ${JSON.stringify(doors())}`);
camera.look({ col: (first.col + second.col) / 2 + 0.5, row: (first.row + second.row) / 2 + 0.5 }, { col: (first.col + second.col) / 2 + 0.5, row: (first.row + second.row) / 2 + 16.5, up: 22 });
await frames(30);
await screenshot("two_stations_loaded");
