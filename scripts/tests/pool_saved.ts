// SPDX-License-Identifier: GPL-3.0-or-later
//
// A park from the game with pools, opened: its pools come in as squares the pool tool works on, their pieces
// and changing rooms placed again from them, saved and loaded again as they were, and on a warm day its guests
// swim. Give it a park the game saved
// with a pool complex and a changing room:
//
//   ./nob script scripts/tests/pool_saved.ts -- --no-intro --no-audio --seed 1 --park PARK

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

const kPool = "Pool/Default/Default_Stub";

await until(() => park.loaded(), 1200, "the park");
await frames(10);
const pieces = map.objects().filter((o) => o.overlay === kPool);
log("pool pieces", pieces.length, [...new Set(pieces.map((o) => o.symbol))].join(" "));
expect(pieces.some((o) => o.symbol === "Base"), "water squares placed from the pool's squares");
expect(pieces.some((o) => o.symbol === "SolidTop"), "paved squares placed from them");

// Saved and loaded again: the squares kept, and the pieces placed from them only once.
async function click_tool(tool: string): Promise<void> {
    await ui.click(`tools/${tool}/${ui.children(`tools/${tool}`)![0]}`);
}
const objects = map.objects().length;
park.setTemperature(28);
await click_tool("Files");
await ui.click("tools/Files/File_SaveGame");
await until(() => ui.find("park_files") !== undefined, 120, "the save window");
await ui.click("park_files/content/open");
await until(() => ui.find("park_files") === undefined, 120, "the save window to close");
park.setTemperature(2); // Back as saved once loaded
if (!ui.find("tools/Files/File_LoadGame"))
    await click_tool("Files");
await until(() => ui.find("tools/Files/File_LoadGame") !== undefined, 120, "the files menu");
await ui.click("tools/Files/File_LoadGame");
const first = "park_files/content/list/view/content/row0";
await until(() => ui.find(first) !== undefined, 120, "the save in the load window");
await ui.click(first);
await ui.click("park_files/content/open");
await until(() => park.loaded() && park.weatherLook().temperature > 20, 1200, "the saved park");
await frames(10);
const again = map.objects().filter((o) => o.overlay === kPool).length;
expect(again === pieces.length, `${pieces.length} pool pieces after loading, not ${again}`);
expect(map.objects().length === objects, `${objects} objects after loading, not ${map.objects().length}`);

park.setWeather("sunny");
park.setTemperature(28);
const swimmers = () => people.list().filter((p) => p.state === "swimming");
await until(() => swimmers().length >= 3, 6000, "guests swimming");
log("swimming", swimmers().length);
const at = swimmers()[0];
camera.look({ col: at.col!, row: at.row! }, { col: at.col!, row: at.row! + 5, up: 4 });
await frames(60);
await screenshot("pool_saved_swimming");
