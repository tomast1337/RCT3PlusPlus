// SPDX-License-Identifier: GPL-3.0-or-later
//
// Lays a saved design the install offers for a corkscrew coaster on the game's blank landscape, picked in the
// designs' window. The construction panel's ring carries the construction crane, which hides and shows the
// strip of controls, and the ride's closed flag; no test light. Its doors are placed, a straight piece clicked
// mid-track is taken out, leaving a gap built on into, and the ride tested: a ghost train runs to the gap and
// crashes off it. Once a path joins it to the park, its own train, tested with the panel open, crashes there too,
// but as a ghost: the park counts no crash.
// Auto-complete then closes the gap again.
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

// Its doors go beside its station, from its window: each tried across the middle of the view until one takes.
const coaster = build.construction()!.ride;
for (const step of ["entrance", "exit"]) {
    rides.show(coaster);
    await frames(5);
    await ui.click(`info/body/Attraction_General_${step === "entrance" ? "Entrance" : "Exit"}`);
    await frames(3);
    expect(build.placing()?.step === step, `placing the ${step}, not ${JSON.stringify(build.placing())}`);
    for (let i = 0; i < 625 && build.placing()?.step === step; ++i) {
        const x = 0.26 + 0.02 * (i % 25);
        const y = 0.2 + 0.02 * Math.floor(i / 25);
        pointer.move(x, y);
        await frames(1);
        if (build.placing()?.problem === "none") {
            await pointer.click(x, y);
            await frames(2);
        }
    }
    expect(build.placing()?.step !== step, `the ${step} placed`);
}
expect(rides.list()[coaster].entrance !== null && rides.list()[coaster].exit !== null, "the doors placed");
if (build.construction() === undefined) {
    rides.show(coaster);
    await frames(5);
    await ui.click("info/Attraction_Construction");
    await frames(5);
}
expect(build.construction()?.ride === coaster, "the construction panel back");

// A click on a piece of the circuit picks it to build over, and demolish takes it out, opening the circuit.
const ride = build.construction()!.ride;
const before = rides.list()[ride].pieces ?? 0;
// A straight piece near the entrance, off the station and not just past it, so the station stays whole and auto-complete can lay a way
// across the gap: looked for on the tiles round the entrance.
const door = rides.list()[ride].entrance!;
const straight = () => {
    const at = build.construction()?.selected;
    const track = rides.list()[ride].track ?? [];
    if (typeof at !== "number")
        return false;
    // Not just past the station, which would leave the train no track to run on.
    const before = track[(at + track.length - 1) % track.length] ?? "";
    return (track[at] ?? "").startsWith("Straight") && !before.startsWith("Station");
};
camera.look({ col: door.col + 0.5, row: door.row + 0.5 }, { col: door.col + 0.5, row: door.row + 12.5, up: 25 });
await frames(5);
for (let i = 0; i < 81 && !straight(); ++i) {
    const at = map.screen(door.col + (i % 9) - 4, door.row + Math.floor(i / 9) - 4);
    if (at === undefined)
        continue;
    await pointer.click(at.x, at.y);
    await frames(3);
}
expect(straight(), "a straight piece picked mid-track");
await screenshot("picked");
await ui.click("placing/body/construction/demolish");
await frames(5);
expect(build.construction()?.selected === null, "building back at the open end");
expect(rides.list()[ride].pieces === before - 1, "the picked piece taken out");
expect(build.construction()?.ghost === true, "the piece taken out offered again in the gap");
expect(build.construction()?.clashes === false, "the gap's piece clear of the pieces either side");
expect(build.construction()?.arrows === true, "arrows where the gap's piece starts and ends");
const gap = build.construction()!.ghostAt!;
camera.look({ col: gap.col + 0.5, row: gap.row - 0.5, up: gap.height + 8 }, { col: gap.col + 0.5, row: gap.row + 3.5, up: gap.height + 18 });
await frames(5);
await screenshot("gap_arrows");

// Tested while its track has a gap, before the park runs it, a ghost of its train runs from the station to the gap
// and crashes off it, its wrecks flying as a crashed train's do. Closing the ride stops the test.
expect(rides.setState(ride, "testing") == null, "the unfinished track tested");
await frames(10);
expect(build.construction()?.testing === true, "a test train running the unfinished track");
const from = build.construction()?.testAt ?? 0;
await frames(60);
expect((build.construction()?.testAt ?? 0) > from, "the test train moving along the track");
await screenshot("testing");
await ui.click("bar/Top_SuperFastForward");
await until(() => (build.construction()?.testWrecks ?? 0) > 0, 18000, "the test train crashed off the gap");
await ui.click("bar/Top_Play");
await frames(20);
await screenshot("ghost_crash");
expect((rides.list()[ride].crashes ?? 0) === 0, "the test train's crash not the park's");
expect(rides.setState(ride, "closed") == null, "the ride closed again");
await frames(3);
expect(build.construction()?.testing === false, "the test train gone once the ride is closed");

// A path beside the entrance, so the park runs the ride and its own train: tried on each side of it till one lays.
await ui.click("tools/Paths/Obj_Paths");
await until(() => ui.find("footpaths/route") !== undefined, 120, "the path panel");
await ui.toggle("footpaths/follow", false);
for (const [dc, dr] of [[0, 1], [1, 0], [0, -1], [-1, 0]]) {
    if (rides.list()[ride].joined)
        break;
    const col = door.col + dc;
    const r = door.row + dr;
    camera.look({ col: col + 0.5, row: r + 0.5 }, { col: col + 0.5, row: r + 10.5, up: 30 });
    await frames(5);
    const at = map.screen(col, r);
    if (at === undefined)
        continue;
    pointer.move(at.x, at.y);
    await frames(10);
    await pointer.click(at.x, at.y);
    await frames(30);
}
build.stop();
await until(() => rides.list()[ride].joined, 300, "the ride run by the park");
rides.show(ride);
await frames(5);
await ui.click("info/Attraction_Construction");
await frames(5);
expect(build.construction()?.ride === ride, "the construction panel back on the ride");

// Tested again while the panel has it, the park's own train runs to the gap and crashes off it, but as a ghost: no
// crash counted.
expect(rides.setState(ride, "testing") == null, "the ride the park runs tested");
await ui.click("bar/Top_SuperFastForward");
await until(() => (rides.list()[ride].wrecks ?? []).length > 0, 18000, "the park's train crashed off the gap");
await ui.click("bar/Top_Play");
expect((rides.list()[ride].crashes ?? 0) === 0, "no crash counted while the panel has the ride");
expect(rides.setState(ride, "closed") == null, "the ride closed once more");
await frames(3);

// Auto-complete looks for a way across the gap round to the station, shows the way it found and lays it on a
// second click.
await ui.click("placing/body/construction/complete");
await frames(1);
expect(build.construction()?.completing === true || (build.construction()?.proposed ?? 0) > 0, "auto-complete looking for a way");
await until(() => build.construction()?.completing === false, 600, "auto-complete done");
expect((build.construction()?.proposed ?? 0) > 0, `a way found round to the station, not ${build.construction()?.complaint}`);
expect(rides.list()[ride].pieces === before - 1, "the way not laid before the second click");
await screenshot("proposed");
await ui.click("placing/body/construction/complete");
await frames(5);
expect(build.construction()?.proposed === 0, "the way taken");
expect(rides.list()[ride].pieces! >= before, "the way laid, closing the circuit");
