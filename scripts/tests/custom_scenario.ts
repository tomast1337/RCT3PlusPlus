// SPDX-License-Identifier: GPL-3.0-or-later
//
// Starts a custom scenario from the first scenario to start from, as the Custom Scenario menu's New does, and
// saves it: the save goes with the custom scenarios. Needs a park in the data folder's new_scenarios/, so run it
// with RCT3PP_DATA set to a scratch folder holding one:
//
//   RCT3PP_DATA=build/cdata ./nob script scripts/tests/custom_scenario.ts

export {};

/** Clicks a front screen button once it has flown in and stopped. */
async function click_front(path: string): Promise<void> {
    let last = -1;
    await until(() => {
        const at = ui.find(path);
        const settled = at !== undefined && at.top >= 0 && Math.abs(at.top - last) < 1e-4;
        last = at?.top ?? -1;
        return settled;
    }, 3000, path);
    await ui.click(path);
}

await until(() => ui.find("front") !== undefined, 1200, "the front screen");
await click_front("front/FrontScreen_MainMenu/FrontScreen_Icon_Play");
await click_front("front/FrontScreen_PlayMenu/FrontScreen_Icon_StartSandbox");
// The Custom Scenario menu is titled like the main menu, as in the game; the main menu has no New.
await click_front("front/FrontScreen_MainMenu/FrontScreen_Icon_NewScenario");
await until(() => ui.find("load_game/content/list/view/content/row0") !== undefined, 300, "a scenario to start from in new_scenarios/");
await screenshot("scenarios_to_start_from");
await ui.click("load_game/content/list/view/content/row0");
await ui.click("load_game/content/open");
await park.ready();
await until(() => ui.find("tools/Files") !== undefined, 300, "the tool row");
await screenshot("custom_scenario_started");

await ui.click(`tools/Files/${ui.children("tools/Files")![0]}`);
await ui.click("tools/Files/File_SaveGame");
await until(() => ui.find("park_files") !== undefined, 120, "the save window");
await ui.click("park_files/content/open");
await until(() => ui.find("park_files") === undefined, 120, "the save window to close");
