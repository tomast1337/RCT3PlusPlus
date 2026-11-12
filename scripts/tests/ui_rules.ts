// SPDX-License-Identifier: GPL-3.0-or-later
//
// What closes what in a park. One panel at a time at the bottom of the screen: opening a tool menu, the map or a
// files window puts the tool out away, the paths button closes the open menu, and an item list goes with its menu.
// A ride's window shares that place: it and a tool's panel put each other away.
//
//   ./nob script scripts/tests/ui_rules.ts -- --no-intro --no-audio --no-simulate --seed 1

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

/** The tool whose menu is open, "" when none is. */
function open_menu(): string {
    return (ui.children("tools") ?? []).filter((tool) => (ui.children(`tools/${tool}`) ?? []).length > 1).join(",");
}

/** Presses the row's button of `tool`, which opens its menu or closes it. */
async function press(tool: string): Promise<void> {
    await ui.click(`tools/${tool}/${ui.children(`tools/${tool}`)![0]}`);
    await frames(4);
}

/** Opens the menu of `tool` unless it is open, and picks `item` from it. */
async function pick(tool: string, item: string): Promise<void> {
    if (open_menu() !== tool)
        await press(tool);
    await ui.click(`tools/${tool}/${item}`);
    await frames(6);
}

async function escape(): Promise<void> {
    keys.press("Escape");
    await frames(4);
}

await park.open("Campaigns/Base/VanillaHills.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 2400, "the build lists");
await frames(10);

// 1. The paths button closes the open menu, and a menu opening puts the path panel away.
await press("Land");
expect(open_menu() === "Land", `the land menu open, not "${open_menu()}"`);
await press("Paths");
expect(open_menu() === "" && ui.find("footpaths") !== undefined, "the paths button to close the land menu and open its panel");
for (const tool of ["Delete", "Land", "Scenery", "Shops", "Rides", "Management", "Files"]) {
    if (ui.find("footpaths") === undefined)
        await press("Paths");
    await press(tool);
    expect(open_menu() === tool && ui.find("footpaths") === undefined, `the ${tool} menu to put the path panel away`);
}
// The view menu's switches are used while building.
await press("Paths");
await press("View");
expect(open_menu() === "View" && ui.find("footpaths") !== undefined, "the path panel to stay under the view menu");
await press("View");

// The land and pool panels go the same way, and so does track building.
await pick("Land", "Land_Brush_Height");
expect(ui.find("land_panel") !== undefined, "the shaping panel");
await press("Rides");
expect(ui.find("land_panel") === undefined, "the rides menu to put the shaping panel away");
await pick("Rides", "Obj_Pools");
expect(ui.find("pool_panel") !== undefined, "the pool panel");
await press("Scenery");
expect(ui.find("pool_panel") === undefined, "the scenery menu to put the pool panel away");
await press("Scenery");
expect(build.pick("tracks/coasters/Corkscrew/Corkscrew", "Corkscrew"), "a corkscrew coaster to build");
await until(() => build.construction() !== undefined, 300, "the construction panel");
await press("Management");
expect(build.construction() === undefined, "the management menu to put track building away");
await screenshot("menu_put_construction_away");

// The map and the files windows put the tool out away too.
await press("Paths");
await ui.click("tools/Map/Map");
await frames(6);
expect(ui.find("park_map") !== undefined && ui.find("footpaths") === undefined, "the map to put the path panel away");
await ui.click("park_map/close");
await frames(6);

// 2. An item list goes with the menu it came from; the scenery list stays out after a pick.
await pick("Rides", "Obj_Rides_ThrillRides");
await until(() => ui.find("build_list") !== undefined, 300, "the thrill rides' list");
await press("Land");
expect(ui.find("build_list") === undefined, "the list to close when another menu opens");
await pick("Scenery", "Obj_Scenery_PathExtras");
await until(() => ui.find("build_list/content/list/view/content") !== undefined, 300, "the path extras' list");
const rows = ui.children("build_list/content/list/view/content")!;
await ui.click(`build_list/content/list/view/content/${rows[rows.length > 1 ? 1 : 0]}`);
await frames(6);
expect(build.placing() !== undefined && ui.find("build_list") !== undefined && open_menu() === "Scenery", "the scenery list and menu to stay once an item is picked");
await screenshot("scenery_list_stays");
await press("Scenery");
expect(ui.find("build_list") === undefined && open_menu() === "", "the list to close with the scenery menu");
build.stop();

// 3. A ride's window and a tool's panel take each other's place.
rides.show(0);
await until(() => ui.find("info") !== undefined, 120, "the ride's window");
await press("Paths");
expect(ui.find("footpaths") !== undefined && ui.find("info") === undefined, "the path panel to put the ride's window away");
rides.show(0);
await until(() => ui.find("info") !== undefined, 120, "the ride's window again");
expect(ui.find("footpaths") === undefined, "the ride's window to put the path panel away");
await pick("Land", "Land_Brush_Height");
expect(ui.find("land_panel") !== undefined && ui.find("info") === undefined, "the land menu and its panel to put the ride's window away");
rides.show(0);
await until(() => ui.find("info") !== undefined, 120, "the ride's window a third time");
expect(ui.find("land_panel") === undefined, "the ride's window to put the shaping panel away");
await screenshot("ride_window_alone");
await ui.click("tools/Map/Map");
await frames(6);
expect(ui.find("park_map") !== undefined && ui.find("info") === undefined, "the map to put the ride's window away");
await ui.click("park_map/close");
await frames(6);

quit(0);
