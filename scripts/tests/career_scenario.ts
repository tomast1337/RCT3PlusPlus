// SPDX-License-Identifier: GPL-3.0-or-later
//
// Starts the first scenario of a career, as its map's New does, saves a game in it and leaves: the save goes
// to the career's folder for that scenario, and leaving goes back to the career's map.
//
//   ./nob script scripts/tests/career_scenario.ts

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

/** Clicks a tool's own button, which comes first in its group. */
async function click_tool(tool: string): Promise<void> {
    await ui.click(`tools/${tool}/${ui.children(`tools/${tool}`)![0]}`);
}

career.start(0);
await park.ready();
await until(() => ui.find("tools/Files") !== undefined, 300, "the tool row");
expect(park.cash() !== undefined, "the scenario to have money");
await screenshot("scenario_started");

await click_tool("Files");
await ui.click("tools/Files/File_SaveGame");
await until(() => ui.find("park_files") !== undefined, 120, "the save window");
await screenshot("save_window");
await ui.click("park_files/content/open");
await until(() => ui.find("park_files") === undefined, 120, "the save window to close");

await click_tool("Leave");
await until(() => ui.find("leave_tutorial") !== undefined, 120, "the question");
await ui.click("leave_tutorial/content/GUI2MiniDialog_Confirm");
await until(() => career.map(), 600, "the career map");
await frames(90);
await screenshot("back_on_the_map");

// Resuming plays the game just saved, still in the career.
await ui.click("career_board/resume");
await until(() => ui.find("career_board_question") !== undefined, 120, "the question");
await ui.click("career_board_question/content/GUI2MiniDialog_Confirm");
await park.ready();
await until(() => ui.find("tools/Files") !== undefined, 300, "the tool row");
await screenshot("resumed");
