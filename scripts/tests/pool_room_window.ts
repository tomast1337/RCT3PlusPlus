// SPDX-License-Identifier: GPL-3.0-or-later
//
// A changing room's window, laid out as an attraction's: a ring of tabs round the room's round view, each page
// in turn (details, finances, thoughts, the wave machines' settings, customers and upkeep), the flag that opens
// and closes the complex with its two lights, and the construction tab that opens the pool panel.
//
//   ./nob script scripts/tests/pool_room_window.ts -- --no-intro --no-audio --seed 1

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

function on_view(col: number, row: number): [number, number] {
    const at = map.screen(col, row);
    if (!at)
        throw new Error(`tile ${col}, ${row} is not in sight`);
    return [at.x, at.y];
}

const kPool = "Pool/Default/Default_Stub";

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Rides/Obj_Pools") !== undefined || ui.find("tools/Rides") !== undefined, 600, "the tool row");
await until(() => build.flatRides() !== undefined, 1200, "the build items");
const gate = map.paths().reduce((a, b) => (b.row > a.row ? b : a));
const c = gate.col;
const r = gate.row - 4;
camera.look({ col: c + 3, row: r + 0.5 }, { col: c + 3, row: r + 12, up: 14 });
await frames(10);

// A small pool east of the road, and a changing room between them; its window opens once it is placed.
const rides = `tools/Rides/${ui.children("tools/Rides")![0]}`;
if (!ui.find("tools/Rides/Obj_Pools"))
    await ui.click(rides);
await until(() => ui.find("tools/Rides/Obj_Pools") !== undefined, 120, "the rides menu");
await ui.click("tools/Rides/Obj_Pools");
await until(() => ui.find("pool_panel/body/editor/Pool_PlaceTiles") !== undefined, 120, "the pool panel");
await ui.click("pool_panel/body/editor/Pool_PlaceTiles");
await frames(2);
await pointer.drag(on_view(c + 2.5, r - 0.5), on_view(c + 7.5, r + 3.5), 8);
await frames(3);
expect(build.pick(kPool, "Pool_DepthNormal"), "the water in the pool list");
await pointer.drag(on_view(c + 3.5, r + 0.5), on_view(c + 6.5, r + 2.5), 6);
await frames(3);
await ui.click("pool_panel/Pool_ChangingRoomPanel");
await frames(2);
await ui.click(`pool_panel/body/${ui.children("pool_panel/body")![0]}`);
await frames(3);
await pointer.click(...on_view(c + 1.5, r + 0.5));
await until(() => ui.find("info") !== undefined, 120, "the changing room's window");
expect(ui.find("pool_panel") === undefined, "the pool panel gone once the room is placed");
camera.look({ col: c + 3, row: r + 1 }, { col: c - 2, row: r + 6, up: 6 });
await frames(5);

// The ring, in the game's order round the view.
const ring = ["Guest_Rides", "Attraction_Finances", "Guest_Thoughts", "Attraction_General_Closed", "Attraction_OpMode", "Attraction_Construction", "Attraction_Customers", "Attraction_Maintenance"];
for (const tab of ring)
    expect(ui.find(`info/${tab}`) !== undefined, `the ${tab} tab, not ${JSON.stringify(ui.children("info"))}`);
await ui.click("info/hide");
await frames(5);
await screenshot("pool_room_window_ring");

// Each page: what it shows, as the game's does.
const body = (name: string) => ui.find(`info/body/${name}`) !== undefined;
const pages: [string, string, string[]][] = [
    ["Guest_Rides", "details", ["ChangingRoomName", "PoolComplexName", "status_label", "Attraction_General_Delete", "GUI2_DeleteSelection", "File_SaveGame", "CenterInViewIcon"]],
    ["Attraction_Finances", "finances", ["AttractionFinancesDialogLabel_AdmissionPrice", "AttractionFinancesDialogLabel_Income_label", "AttractionFinancesDialogLabel_CostPerInspection_label", "AttractionFinancesDialogLabel_ProjectedRunningCost_label", "AttractionFinancesDialogLabel_Profit_label", "AttractionFinancesDialogLabel_TotalProfit_label"]],
    ["Guest_Thoughts", "thoughts", []],
    ["Attraction_OpMode", "waves", ["WaveHeightText", "WaveLengthText", "WaveOnPeriodText", "WaveTotalPeriodText"]],
    ["Attraction_Customers", "customers", ["AttractionCustomersDialogLabel_Customers_label", "AttractionCustomersDialogLabel_Popularity_label", "AttractionCustomersDialogLabel_Satisfaction_label", "AttractionCustomersDialogLabel_TotalCustomers_label", "AttractionCustomersDialogLabel_AgeYears_label"]],
    ["Attraction_Maintenance", "maintenance", ["RideMaintenanceDialogLabel_Reliability_label", "RideMaintenanceDialogLabel_Inspection", "Fluid_TypeLabel", "RideMaintenanceDialogLabel_TimeSinceLastInspection_label", "PoolDirt_Label_label", "Staff_JobIsMechanicIcon"]],
];
for (const [tab, page, names] of pages)
{
    await ui.click(`info/${tab}`);
    await frames(3);
    for (const name of names)
        expect(body(name), `${name} on the ${page} page, not ${JSON.stringify(ui.children("info/body"))}`);
    await screenshot(`pool_room_window_${page}`);
}
await ui.click("info/Guest_Rides");
await frames(3);
expect(ui.find("info/body/status_label")?.text === "Closed", `the complex closed at first, not ${ui.find("info/body/status_label")?.text}`);

// The flag fans out its two lights; the green one opens the complex, the red one closes it again.
await ui.click("info/Attraction_General_Closed");
await until(() => ui.find("info/Attraction_Button_Open") !== undefined, 60, "the flag's lights");
expect(ui.find("info/Attraction_Button_Closed") !== undefined, "the red light beside the green");
await screenshot("pool_room_window_flag");
await ui.click("info/Attraction_Button_Open");
await until(() => ui.find("info/Attraction_General_Open") !== undefined, 60, "the flag showing open");
await ui.click("info/Guest_Rides");
await frames(3);
log("open status", ui.find("info/body/status_label")?.text);
expect(ui.find("info/body/status_label")?.text?.includes("pool complex") ?? false, `the complex open, not ${ui.find("info/body/status_label")?.text}`);
await ui.click("info/Attraction_General_Open");
await until(() => ui.find("info/Attraction_Button_Closed") !== undefined, 60, "the flag's lights again");
await ui.click("info/Attraction_Button_Closed");
await until(() => ui.find("info/Attraction_General_Closed") !== undefined, 60, "the flag showing closed");
await frames(3);
expect(ui.find("info/body/status_label")?.text === "Closed", `the complex closed, not ${ui.find("info/body/status_label")?.text}`);

// The construction tab opens the pool panel, the window staying.
await ui.click("info/Attraction_Construction");
await until(() => ui.find("pool_panel") !== undefined, 120, "the pool panel");
expect(ui.find("info") !== undefined, "the room's window still open");
await frames(3);
await screenshot("pool_room_window_construction");
