// SPDX-License-Identifier: GPL-3.0-or-later
//
// What closes what in a park. One panel at a time at the bottom of the screen: opening a tool menu, the map or a
// files window puts the tool out away, the paths button closes the open menu, and an item list goes with its menu.
// A ride's window shares that place: it and a tool's panel put each other away. The path panel starts clean each
// time it opens. Escape closes what is on top, one thing a press, and asks to leave only once nothing is left.
// Loading asks first whether to save. A right click leaves the delete and structure tools out.
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

/** What the path panel's band tells with the pointer on it, off the ground where it would tell what a click lays. */
async function band(): Promise<string | undefined> {
    pointer.move(ui.find("footpaths/status")!.x, ui.find("footpaths/status")!.y);
    await frames(4);
    return ui.find("footpaths/status")?.text;
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

// 4. The path panel starts clean: what was switched on is off again, and no trail goes on.
await press("Paths");
const fresh = await band();
await ui.click("footpaths/remove");
await ui.click("footpaths/straight");
await frames(2);
expect(ui.find("footpaths/remove")?.on === true && ui.find("footpaths/straight")?.on === true, "the remove and straight tabs switched on");
await ui.click("footpaths/remove");
await pointer.click(0.55, 0.5);
await frames(6);
expect((await band()) !== fresh, `a trail started on the ground, the band no longer "${fresh}"`);
await screenshot("path_trail_started");
await ui.click("footpaths/close");
await frames(4);
await press("Paths");
expect(ui.find("footpaths/remove")?.on === false && ui.find("footpaths/straight")?.on === false && ui.find("footpaths/route")?.on === false, "the tabs off again");
expect((await band()) === fresh, `no trail going on, the band "${fresh}" again`);
await screenshot("path_panel_clean");
await press("Paths");

// 5. Escape closes what is on top, one thing a press: the windows from the top down, the item list, the menu, a
// path trail and then the path panel, a ride's window, and only then asks to leave.
const question = "leave_tutorial";
await pick("Management", "Man_Finances");
await pick("Files", "File_GameOptions");
await until(() => ui.find("options") !== undefined, 120, "the options window");
await pick("Files", "File_AboutRCT");
await until(() => ui.find("about") !== undefined, 120, "the about window");
await screenshot("windows_stacked");
await escape();
expect(ui.find("about") === undefined && ui.find("options") !== undefined, "the first Escape to close the about window, on top");
await escape();
expect(ui.find("options") === undefined && ui.find("FinancialManagement_Title") !== undefined, "the second to close the options, over the finances");
await escape();
expect(ui.find("FinancialManagement_Title") === undefined && open_menu() === "Files", "the third to close the finances");
await escape();
expect(open_menu() === "" && ui.find(question) === undefined, "the fourth to close the menu");

await ui.click("tools/Map/Map");
await frames(6);
await escape();
expect(ui.find("park_map") === undefined && ui.find(question) === undefined, "Escape to close the map");

await pick("Rides", "Obj_Rides_ThrillRides");
await until(() => ui.find("build_list") !== undefined, 300, "the thrill rides' list");
await pointer.click(0.9, 0.3); // The list keeps no keyboard focus
await frames(2);
await escape();
expect(ui.find("build_list") === undefined && open_menu() === "Rides", "Escape to close the item list before its menu");
await escape();
expect(open_menu() === "", "and then the menu");

await press("Paths");
await pointer.click(0.55, 0.5);
await frames(6);
expect((await band()) !== fresh, "a trail started");
await escape();
expect(ui.find("footpaths") !== undefined && (await band()) === fresh, "Escape to let the trail go and leave the panel");
await escape();
expect(ui.find("footpaths") === undefined && ui.find(question) === undefined, "and then to close the path panel");

expect(build.pick("tracks/coasters/Corkscrew/Corkscrew", "Corkscrew"), "a corkscrew coaster to build");
await until(() => build.construction() !== undefined, 300, "the construction panel");
await escape();
expect(build.construction() === undefined && ui.find(question) === undefined, "Escape to close track building");

rides.show(0);
await until(() => ui.find("info") !== undefined, 120, "the ride's window");
await escape();
expect(ui.find("info") === undefined && ui.find(question) === undefined, "Escape to close the ride's window");

await escape();
expect(ui.find(question) !== undefined, "Escape to ask to leave once nothing is left");
const asked = ui.find(`${question}/content/text`)?.text ?? "";
expect(asked.length > 0 && !asked.includes("tutorial"), `the question not to speak of a tutorial outside one: "${asked}"`);
await screenshot("leave_question");
await escape();
expect(ui.find(question) === undefined, "Escape to answer no");

// 6. Loading asks first whether to save: yes opens the save window, no the load window, cancel neither.
await pick("Files", "File_LoadGame");
await until(() => ui.find("load_question") !== undefined, 120, "the question before loading");
expect(ui.find("park_files") === undefined, "no file window before the answer");
await screenshot("load_question");
await escape();
expect(ui.find("load_question") === undefined && ui.find("park_files") === undefined, "Escape to cancel loading");
await pick("Files", "File_LoadGame");
await until(() => ui.find("load_question") !== undefined, 120, "the question again");
await ui.click("load_question/content/answer1");
await until(() => ui.find("park_files") !== undefined, 120, "the load window after no");
log("files", JSON.stringify(ui.children("park_files")), JSON.stringify(ui.children("park_files/content")), JSON.stringify(ui.find("park_files/title")));
await screenshot("load_window");
await escape();
expect(ui.find("park_files") === undefined, "Escape to close the load window");
await pick("Files", "File_LoadGame");
await until(() => ui.find("load_question") !== undefined, 120, "the question a third time");
await ui.click("load_question/content/answer0");
await until(() => ui.find("park_files") !== undefined, 120, "the save window after yes");
await screenshot("save_window");
await escape();

// 7. A right click deletes what is under the pointer and leaves the tool out (scripts/tests/right_click_delete.ts
// checks what goes); here the area delete and the picking out tools stay after one.
await pick("Delete", "GUI2_DeleteSelection");
await until(() => ui.find("delete_area") !== undefined, 120, "the area delete panel");
await pointer.click(0.55, 0.5, "right");
await frames(4);
expect(ui.find("delete_area") !== undefined, "the area delete tool to stay out after a right click");
await screenshot("right_click_keeps_tool");
await escape();
await escape();
expect(ui.find("delete_area") === undefined && open_menu() === "", "Escape to close the menu and then the tool");

quit(0);
