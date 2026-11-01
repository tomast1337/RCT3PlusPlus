// SPDX-License-Identifier: GPL-3.0-or-later
//
// Builds a station of five pieces for a steel coaster, a wooden coaster, the mini railway and the haunted hotel on
// the game's blank landscape: each station piece stands in its platform model, of the type its ride's train names
// (or its kind's, when the train names none), capped at the station's two ends. The steel coaster's park is saved
// and loaded again, and its platforms are still there.
//
//   ./nob script scripts/tests/station_models.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

/** Builds a station of `kind` and gives its ride's place in rides.list(). */
async function station(overlay: string, kind: string): Promise<number> {
    await park.open("Campaigns/Base/BlankLandscape.dat");
    await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "tools");
    expect(build.pick(overlay, kind, "designs"), `a ${kind} to build`);
    await frames(30);
    // Kinds with saved designs open their window first; build a custom track.
    if (ui.find("design_browser") !== undefined) {
        await ui.click("design_browser/content/build");
        await frames(10);
    }
    const ride = build.construction()!.ride;
    const { width, height } = map.size();
    at = { col: Math.floor(width / 2), row: Math.floor(height / 2) };
    view();
    await frames(5);
    const p = map.screen(at.col, at.row)!;
    pointer.move(p.x, p.y);
    await frames(10);
    // The plain station among the pictured pieces, clicked again to build it.
    let special: string | undefined;
    for (let i = 0; i < 8 && special === undefined; ++i) {
        const button = `placing/body/construction/special${i}`;
        if (ui.find(button) === undefined)
            continue;
        await ui.click(button);
        await frames(10);
        if (/^station/i.test(build.construction()?.piece ?? ""))
            special = button;
    }
    expect(special !== undefined, `a ${kind} station piece to build`);
    await frames(20);
    await pointer.click(p.x, p.y);
    await frames(30);
    for (let n = 0; n < 4; ++n) {
        await ui.click(special!);
        await frames(10);
    }
    build.stop();
    await ui.click("placing/close");
    await frames(10);
    return ride;
}

// The middle of the map, where the station starts.
let at = { col: 0, row: 0 };
const view = () => camera.look({ col: at.col + 2.5, row: at.row + 0.5 }, { col: at.col - 1, row: at.row + 5, up: 4 });

function check(ride: number, kind: string, type: RegExp): string[] {
    const built = rides.list()[ride];
    log(kind, JSON.stringify({ track: built.track, platforms: built.platforms }));
    expect(built.track?.length === 5, `five ${kind} station pieces, not ${JSON.stringify(built.track)}`);
    const platforms = built.platforms ?? [];
    expect(platforms.length === 5 && platforms.every((p) => type.test(p)), `a ${type} platform model on each ${kind} station piece, not ${JSON.stringify(platforms)}`);
    expect(/start/i.test(platforms[0]) && /middle/i.test(platforms[2]) && /end/i.test(platforms[4]), `the ${kind} station capped at both ends, not ${JSON.stringify(platforms)}`);
    return platforms;
}

for (const [overlay, kind, type] of [
    ["tracks/coasters/WoodenRC/WoodenRC", "WoodenRC", /^wooden/i],
    ["tracks/TrackedRides/MinRail/MinRail", "MinRail", /^standard/i],
    ["tracks/TrackedRides/HauntedMansion/HauntedMansion", "HauntedMansion", /^noplatform/i],
] as [string, string, RegExp][]) {
    const ride = await station(overlay, kind);
    check(ride, kind, type);
    view();
    await frames(30);
    await screenshot(kind);
}

const ride = await station("tracks/coasters/LoopingRC/LoopingRC", "LoopingRC");
const platforms = check(ride, "LoopingRC", /^standard/i);
view();
await frames(30);
await screenshot("LoopingRC");

// Saved and loaded again.
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
const loaded = rides.list()[ride];
log("loaded", JSON.stringify({ track: loaded.track, platforms: loaded.platforms }));
view();
await frames(30);
await screenshot("loaded");
expect(JSON.stringify(loaded.platforms) === JSON.stringify(platforms), `the platforms back after loading, not ${JSON.stringify(loaded.platforms)}`);
