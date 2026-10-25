// SPDX-License-Identifier: GPL-3.0-or-later
//
// The graphs of a park that has run a while: the park window's (rating, guests and the rest), the finances'
// (cash, values, profit) and a tested coaster's (speed, height and forces).
//
//   ./nob script scripts/tests/graphs.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

/** The path of the widget named `name` somewhere under `path`. */
function under(path: string, name: string): string | undefined {
    for (const child of ui.children(path) ?? []) {
        const at = `${path}/${child}`;
        if (child === name)
            return at;
        const found = under(at, name);
        if (found !== undefined)
            return found;
    }
    return undefined;
}

async function management(item: string, title: string, tab: string): Promise<void> {
    if (ui.find(`tools/Management/${item}`) === undefined)
        await ui.click(`tools/Management/${ui.children("tools/Management")![0]}`);
    await until(() => ui.find(`tools/Management/${item}`) !== undefined, 120, "the management menu");
    await ui.click(`tools/Management/${item}`);
    await until(() => under(title, tab) !== undefined, 120, `the ${tab} tab`);
    await ui.click(under(title, tab)!);
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
const tick = under("FinancialManagement_Title", "Graph_WeeklyProfit_tick")!;
expect(ui.find(tick)?.on === true, "every series ticked at first");
await ui.click(tick);
await frames(90);
expect(ui.find(tick)?.on === false, "the weekly profit unticked");
await screenshot("finance_graphs");
await close("FinancialManagement_Title");

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
quit(0);
