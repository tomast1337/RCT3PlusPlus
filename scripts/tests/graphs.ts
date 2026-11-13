// SPDX-License-Identifier: GPL-3.0-or-later
//
// The graphs of a park that has run a while: the park window's (rating, guests and the rest), the finances'
// (cash, values, profit), kept with the ledger through a save and a reload, and a tested coaster's (speed, height
// and forces).
//
//   ./nob script scripts/tests/graphs.ts -- --no-audio

export {};

/** The path of the widget named `name` somewhere under `path`. */

async function management(item: string, title: string, tab: string): Promise<void> {
    if (ui.find(`tools/Management/${item}`) === undefined)
        await ui.click(`tools/Management/${must(ui.children("tools/Management"), "tools/Management shown")[0]}`);
    await until(() => ui.find(`tools/Management/${item}`) !== undefined, 120, "the management menu");
    await ui.click(`tools/Management/${item}`);
    await until(() => under(title, tab) !== undefined, 120, `the ${tab} tab`);
    await ui.click(must(under(title, tab), `${tab} under ${title}`));
    await frames(10);
}

await park.open("Campaigns/Base/Gunslinger.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined, 1200, "the park");
await frames(30);

async function close(title: string): Promise<void> {
    const button = under(title, "close");
    if (button !== undefined)
        await ui.click(button);
    await frames(5);
}

// Two weeks end, each adding to the weekly graphs.
for (let week = 0; week < 2; ++week) {
    park.setDate((Math.floor(park.date() / 90) + 1) * 90 - 0.05);
    await frames(30);
}

await management("Man_Park", "PID_Title", "ParkInfoDialogIcon_Graphs");
expect(under("PID_Title", "graph") !== undefined, "the park's graph");
// A tick box for each series, the guests by age among them.
for (const key of ["Graph_ParkValue", "Graph_AdultsInPark", "Graph_Guests", "Graph_PublicityLevel", "Graph_ParkRating"])
    expect(under("PID_Title", `${key}_tick`) !== undefined, `a tick box for ${key}`);
await screenshot("park_graphs");
await close("PID_Title");
await management("Man_Finances", "FinancialManagement_Title", "CashGraphTabIcon");
expect(under("FinancialManagement_Title", "graph") !== undefined, "the finances' graph");
// Unticked, a series stays unticked when the page comes again.
const tick = must(under("FinancialManagement_Title", "Graph_WeeklyProfit_tick"), "Graph_WeeklyProfit_tick under FinancialManagement_Title");
expect(ui.find(tick)?.on === true, "every series ticked at first");
await ui.click(tick);
await frames(90);
expect(ui.find(tick)?.on === false, "the weekly profit unticked");
await screenshot("finance_graphs");
await close("FinancialManagement_Title");

// Saved and opened again, paused so nothing is booked between: the graphs and the ledger as they were.
await ui.click("bar/Top_Pause");
await frames(5);
const before = JSON.stringify(park.history());
await files_menu("File_SaveGame");
await until(() => ui.find("park_files") !== undefined, 120, "the save window");
await ui.click("park_files/content/open");
await until(() => ui.find("park_files") === undefined, 120, "the save window to close");
await files_menu("File_LoadGame");
// Loading asks first whether to save; the park was saved just now.
await until(() => ui.find("load_question") !== undefined, 120, "the question before loading");
await ui.click("load_question/content/answer1");
await until(() => ui.find("park_files/content/list/view/content/row0") !== undefined, 120, "the save in the load window");
await ui.click("park_files/content/list/view/content/row0");
await ui.click("park_files/content/open");
await frames(5);
await until(() => park.loaded(), 1200, "the saved park");
const after = JSON.stringify(park.history());
log("history:", before);
expect(after === before, `the history as saved, not ${after}`);
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined, 1200, "the saved park's tools");
await frames(30);

// A coaster's lap run here replaces the graphs it was saved with.
const coaster = rides.list().findIndex((r) => !r.flat && !r.demolished && r.open === true && r.graphSamples > 0);
expect(coaster >= 0, "an open coaster with graphs");
const saved = rides.list()[coaster].graphSamples;
await ui.click("bar/Top_SuperFastForward");
await until(() => rides.list()[coaster].graphSamples !== saved, 3600, "a lap's graphs");
// The first lap was joined part way round; the next is whole.
const part = rides.list()[coaster].graphSamples;
await until(() => rides.list()[coaster].graphSamples !== part, 3600, "a whole lap's graphs");
log("lap graphs:", rides.list()[coaster].name, saved, "then", part, "then", rides.list()[coaster].graphSamples);
await ui.click("bar/Top_Play");

// The first ride with a graph tab and a test to show on it.
const list = rides.list();
let shown = false;
for (let i = 0; i < list.length && !shown; ++i) {
    if (list[i].flat || list[i].demolished)
        continue;
    rides.show(i);
    await frames(5);
    if (ui.find("info/Attraction_Graphs") === undefined)
        continue;
    await ui.click("info/Attraction_Graphs");
    await frames(5);
    if (under("info", "graph") === undefined)
        continue;
    // Every series on.
    for (const key of ["Graph_Altitude", "Graph_VerticalG", "Graph_LateralG"]) {
        const tick = under("info", key);
        if (tick !== undefined)
            await ui.click(tick);
    }
    await frames(5);
    log("ride graphs:", list[i].name);
    shown = true;
}
expect(shown, "a coaster with test graphs");
await screenshot("ride_graphs");
await ui.click("info/close").catch(() => undefined);

// A new sandbox, with no graphs kept, starts them as its weeks end.
await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined, 1200, "the sandbox");
await frames(30);
for (let week = 0; week < 3; ++week) {
    park.setDate((Math.floor(park.date() / 90) + 1) * 90 - 0.05);
    await frames(30);
}
await management("Man_Finances", "FinancialManagement_Title", "CashGraphTabIcon");
expect(under("FinancialManagement_Title", "graph") !== undefined, "the sandbox's finance graph");
await screenshot("sandbox_graphs");
quit(0);
