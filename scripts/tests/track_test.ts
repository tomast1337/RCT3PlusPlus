// SPDX-License-Identifier: GPL-3.0-or-later
//
// Lays a saved design the install offers for a corkscrew coaster on the game's blank landscape, picked in the
// designs' window. The construction panel's ring carries the construction crane, which hides and shows the
// strip of controls, and the ride's closed flag; no test light. A piece clicked mid-track is then taken out,
// leaving a gap built on into. Before that,
// auto-complete closes the circuit opened at its last piece.
//
//   ./nob script scripts/tests/track_test.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "tools");
expect(build.pick("tracks/coasters/Corkscrew/Corkscrew", "Corkscrew", "designs"), "a corkscrew coaster to build");
await until(() => ui.find("design_browser") !== undefined, 600, "the designs window");
await frames(3);
const designs = build.construction()?.designs ?? [];
expect(designs.length > 0, "a saved corkscrew design");
expect(ui.find("placing/Attraction_Construction") !== undefined, "the construction crane on the ring");
expect(ui.find("placing/Attraction_General_Closed") !== undefined, "the closed flag on the ring");
expect(ui.find("placing/Attraction_Button_Testing") === undefined && ui.find("placing/Attraction_Button_Testing_Disabled") === undefined, "no test light on the ring");

// The window opens on the custom track, the first row; a design is the row below it, a heading and two rows
// down in the window's units (a button is 22 of them high).
const list = ui.find("design_browser/content/list")!;
const button = ui.find("design_browser/content/build")!;
const unit = (button.bottom - button.top) / 22;
await pointer.click(list.x, list.top + (18 + 16 * 2) * unit);
await frames(5);
expect(ui.find("design_browser/content/details/lines") !== undefined, "the design's details");
await screenshot("designs");
await ui.click("design_browser/content/build");
await frames(10);
expect(ui.find("design_browser") === undefined, "the window closed by Build");
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

// Demolish opens the circuit at its last piece. Auto-complete looks for a way round to the station from there, shows
// the way it found and lays it on a second click.
const laid = rides.list()[build.construction()!.ride].pieces ?? 0;
await ui.click("placing/body/construction/demolish");
await frames(5);
expect(rides.list()[build.construction()!.ride].pieces === laid - 1, "the circuit opened at its last piece");
await ui.click("placing/body/construction/complete");
await frames(1);
expect(build.construction()?.completing === true || (build.construction()?.proposed ?? 0) > 0, "auto-complete looking for a way");
await until(() => build.construction()?.completing === false, 600, "auto-complete done");
expect((build.construction()?.proposed ?? 0) > 0, `a way found round to the station, not ${build.construction()?.complaint}`);
expect(rides.list()[build.construction()!.ride].pieces === laid - 1, "the way not laid before the second click");
await screenshot("proposed");
await ui.click("placing/body/construction/complete");
await frames(5);
expect(build.construction()?.proposed === 0, "the way taken");
expect(rides.list()[build.construction()!.ride].pieces! >= laid, "the way laid, closing the circuit");

// A click on a piece of the circuit picks it to build over, and demolish takes it out, opening the circuit.
const ride = build.construction()!.ride;
const before = rides.list()[ride].pieces ?? 0;
// The design is laid round the point clicked; look a little round it for a piece.
for (let i = 0; i < 25 && typeof build.construction()?.selected !== "number"; ++i) {
    await pointer.click(0.48 + 0.01 * (i % 5), 0.43 + 0.01 * Math.floor(i / 5));
    await frames(3);
}
expect(typeof build.construction()?.selected === "number", "a piece picked mid-track");
await screenshot("picked");
await ui.click("placing/body/construction/demolish");
await frames(5);
expect(build.construction()?.selected === null, "building back at the open end");
expect(rides.list()[ride].pieces === before - 1, "the picked piece taken out");
expect(build.construction()?.ghost === true, "the piece taken out offered again in the gap");
expect(build.construction()?.clashes === false, "the gap's piece clear of the pieces either side");

