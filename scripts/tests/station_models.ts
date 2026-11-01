// SPDX-License-Identifier: GPL-3.0-or-later
//
// Builds a corkscrew coaster's station of three pieces on the game's blank landscape: each station piece stands in
// its platform model, capped at the station's two ends. Saved and loaded again, the platforms are still there.
//
//   ./nob script scripts/tests/station_models.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "tools");
expect(build.pick("tracks/coasters/Corkscrew/Corkscrew", "Corkscrew", "designs"), "a corkscrew coaster to build");
await until(() => ui.find("design_browser") !== undefined, 600, "the designs window");
await frames(3);
await ui.click("design_browser/content/build");
await frames(10);
const ride = build.construction()!.ride;
const { width, height } = map.size();
const at = { col: Math.floor(width / 2), row: Math.floor(height / 2) };
const view = () => camera.look({ col: at.col + 0.5, row: at.row + 0.5 }, { col: at.col + 6.5, row: at.row + 9.5, up: 9 });
view();
await frames(5);
const p = map.screen(at.col, at.row)!;
pointer.move(p.x, p.y);
await frames(10);

// The station among the pictured pieces, clicked again to build it.
let special: string | undefined;
for (let i = 0; i < 8 && special === undefined; ++i) {
    const button = `placing/body/construction/special${i}`;
    if (ui.find(button) === undefined)
        continue;
    await ui.click(button);
    await frames(10);
    if ((build.construction()?.piece ?? "").toLowerCase().includes("station"))
        special = button;
}
expect(special !== undefined, "a station piece to build");
await frames(20);
await pointer.click(p.x, p.y);
await frames(30);
for (let n = 0; n < 2; ++n) {
    await ui.click(special!);
    await frames(10);
}
const built = rides.list()[ride];
log("built", JSON.stringify({ track: built.track, platforms: built.platforms }));
expect(built.track?.length === 3, `three station pieces, not ${JSON.stringify(built.track)}`);
const platforms = built.platforms ?? [];
expect(platforms.length === 3, `a platform model on each station piece, not ${JSON.stringify(platforms)}`);
expect(/start/i.test(platforms[0]) && /middle/i.test(platforms[1]) && /end/i.test(platforms[2]), `the station capped at both ends, not ${JSON.stringify(platforms)}`);
await screenshot("built");

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
