// SPDX-License-Identifier: GPL-3.0-or-later
//
// A park from the game with pools, opened: its pools come in as squares the pool tool works on, their pieces
// and changing rooms placed again from them, saved and loaded again as they were, and on a warm day its guests
// swim. Give it a park the game saved
// with a pool complex and a changing room (best with guests swimming, sun loungers and a spa pool):
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
// Those the park had swimming swim on in its pools, not out on the paths.
const saved_swimmers = people.list().filter((p) => p.state === "swimming").length;
log("swimming as saved", saved_swimmers);
expect(saved_swimmers > 0, "the park's swimmers still in the water");

// Its changing room, there once, opens its complex's window, where the complex opens.
const room = map.objects().find((o) => o.overlay !== kPool && o.symbol.startsWith("CRoom"));
expect(room !== undefined, "a changing room");
expect(map.objects().filter((o) => o.col === room!.col && o.row === room!.row && o.symbol === room!.symbol).length === 1, "the changing room there once");
camera.look({ col: room!.col + 0.5, row: room!.row + 0.5 }, { col: room!.col + 0.5, row: room!.row + 6, up: 5 });
await frames(10);
// Its roof, above the guests' heads.
const middle = map.screen(room!.col + 0.5, room!.row + 0.5)!;
await pointer.click(middle.x, middle.y - 0.12);
await until(() => ui.find("info/Attraction_OpMode") !== undefined, 120, "the changing room's window");
await ui.click("info/Attraction_OpMode");
await frames(3);
const state = () => ui.find("info/body/AttractionStatusOpen_label")?.text;
const saved_state = state();
const choice = ui.find("info/body/AttractionStatusOpen")!;
await pointer.drag([choice.x, choice.y], [choice.x, choice.bottom + (choice.bottom - choice.top) / 2], 6);
await frames(3);
log("changing room", saved_state, "then", state());
expect(state() === "Open", `the complex open, not ${state()}`);
await screenshot("pool_saved_room_open");
await ui.click("info/close");

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
// Its swimmers saved with it, still in the water.
const reloaded_swimmers = people.list().filter((p) => p.state === "swimming").length;
log("swimming after loading", reloaded_swimmers);
expect(reloaded_swimmers >= saved_swimmers * 0.9, `most of the ${saved_swimmers} swimmers still swimming, not ${reloaded_swimmers}`);

park.setWeather("sunny");
park.setTemperature(28);
const swimmers = () => people.list().filter((p) => p.state === "swimming");
await until(() => swimmers().length >= 3, 6000, "guests swimming");
log("swimming", swimmers().length);
const at = swimmers()[0];
camera.look({ col: at.col!, row: at.row! }, { col: at.col!, row: at.row! + 5, up: 4 });
await frames(60);
await screenshot("pool_saved_swimming");

// Now and then a swimmer sits in the spa pool, in the spa's water.
const spa = map.objects().find((o) => o.overlay === kPool && o.symbol === "Jacuzzi");
if (spa) {
    log("spa at", spa.col, spa.row);
    const sitting = () => swimmers().find((p) => Math.abs(p.col! - spa.col - 0.5) < 0.05 && Math.abs(p.row! - spa.row - 0.5) < 0.05);
    await ui.click("bar/Top_SuperFastForward");
    await until(() => sitting() !== undefined, 20000, "a swimmer in the spa pool");
    await ui.click("bar/Top_Play");
    await frames(120);
    camera.look({ col: spa.col + 0.5, row: spa.row + 0.5, up: 0.3 }, { col: spa.col + 1.3, row: spa.row + 0.9, up: 0.9 });
    await frames(4);
    await screenshot("pool_saved_spa");
}

// Swimmers lie on the sun loungers the park placed, and stand under its showers.
const extra = map.objects().find((o) => o.symbol.startsWith("SunLoungers") || o.symbol === "PoolShower");
if (extra) {
    log("extra", extra.symbol, "at", extra.col, extra.row);
    const using_it = () => swimmers().find((p) => p.pool_extra !== undefined && Math.floor(p.col!) === extra.col && Math.floor(p.row!) === extra.row);
    await ui.click("bar/Top_SuperFastForward");
    await until(() => using_it() !== undefined, 20000, `a swimmer on the ${extra.symbol}`);
    await ui.click("bar/Top_Play");
    log("using it", JSON.stringify(using_it()));
    park.setTime(0.5); // Seen by day
    camera.look({ col: extra.col + 0.5, row: extra.row + 0.5 }, { col: extra.col + 2, row: extra.row + 2, up: 1.2 });
    await frames(30);
    await screenshot("pool_saved_extra");
}
