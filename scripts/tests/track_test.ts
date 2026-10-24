// SPDX-License-Identifier: GPL-3.0-or-later
//
// Lays the first saved design the install offers for a corkscrew coaster on the game's blank landscape, from the
// construction panel. The panel's ring carries the construction crane, which hides and shows the strip of
// controls, and the ride's closed flag; no test light.
//
//   ./nob script scripts/tests/track_test.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "tools");
expect(build.pick("tracks/coasters/Corkscrew/Corkscrew", "Corkscrew"), "a corkscrew coaster to build");
await frames(5);
const designs = build.construction()?.designs ?? [];
expect(designs.length > 0, "a saved corkscrew design");
expect(ui.find("placing/Attraction_Construction") !== undefined, "the construction crane on the ring");
expect(ui.find("placing/Attraction_General_Closed") !== undefined, "the closed flag on the ring");
expect(ui.find("placing/Attraction_Button_Testing") === undefined && ui.find("placing/Attraction_Button_Testing_Disabled") === undefined, "no test light on the ring");

// The designs list opens with the custom track first; the first saved design is the row below it.
await ui.click("construction_design");
await frames(3);
const menu = ui.find("construction_design.menu");
expect(menu !== undefined, "the designs list open");
const row = (menu!.bottom - menu!.top) / (designs.length + 1);
await pointer.click(menu!.x, menu!.top + row * 1.5);
await frames(10);
pointer.move(0.5, 0.45);
await frames(30);
await pointer.click(0.5, 0.45);
await frames(30);
expect(rides.list().some((r) => (r.pieces ?? 0) > 0), "the design laid");

// The crane hides the strip, and shows it again.
await ui.click("placing/Attraction_Construction");
await frames(3);
expect(ui.find("placing/body/construction") === undefined, "the strip hidden by the crane");
await ui.click("placing/Attraction_Construction");
await frames(3);
expect(ui.find("placing/body/construction") !== undefined, "the strip shown again by the crane");
await screenshot("laid");
