// SPDX-License-Identifier: GPL-3.0-or-later
//
// Lays the first saved design the install offers for a corkscrew coaster on the game's blank landscape, pauses
// the park and runs a test train round it from the construction panel's ring, as a player checks a track before
// opening it. The test may only start once the circuit is closed, must run while the park is paused, and must
// stop at a second press.
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
expect(ui.find("placing/Attraction_Button_Testing_Disabled") !== undefined, "the test greyed before any track");

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
expect(ui.find("placing/Attraction_Button_Testing") !== undefined, "the test offered once the design is laid");

await ui.click("bar/Top_Pause");
await frames(5);
await ui.click("placing/Attraction_Button_Testing");
await frames(60);
expect(build.construction()?.testing === true, "a test train running while paused");
await screenshot("testing");
await ui.click("placing/Attraction_Button_Testing");
await frames(5);
expect(build.construction()?.testing === false, "the test stopped by a second press");
