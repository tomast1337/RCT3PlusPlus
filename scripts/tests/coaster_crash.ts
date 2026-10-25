// SPDX-License-Identifier: GPL-3.0-or-later
//
// Lays a saved design the install offers for a corkscrew coaster on the game's blank landscape, takes a
// piece out mid-track so the circuit no longer joins up, gives its entrance a path and tests it. Its train runs
// off the end of the track and crashes: the cars fly on and blow up, and the ride is closed.
//
//   ./nob script scripts/tests/coaster_crash.ts -- --no-audio

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

// The designs window opens on the custom track, the first row; the first saved design is a heading and two rows
// below it, in the window's units (a button is 22 of them high).
const list = ui.find("design_browser/content/list")!;
const button = ui.find("design_browser/content/build")!;
const unit = (button.bottom - button.top) / 22;
await pointer.click(list.x, list.top + (18 + 16 * 2) * unit);
await frames(5);
await ui.click("design_browser/content/build");
await frames(10);
pointer.move(0.5, 0.45);
await frames(30);
await pointer.click(0.5, 0.45);
await frames(30);
const ride = build.construction()!.ride;
expect((rides.list()[ride].pieces ?? 0) > 0, "the design laid");

// A piece of the circuit taken out leaves it open.
const before = rides.list()[ride].pieces ?? 0;
for (let i = 0; i < 25 && typeof build.construction()?.selected !== "number"; ++i) {
    await pointer.click(0.48 + 0.01 * (i % 5), 0.43 + 0.01 * Math.floor(i / 5));
    await frames(3);
}
expect(typeof build.construction()?.selected === "number", "a piece picked mid-track");
await ui.click("placing/body/construction/demolish");
await frames(5);
expect(rides.list()[ride].pieces === before - 1, "the picked piece taken out");
build.stop();
await frames(5);

// A path beside the entrance, so the park runs the ride: tried on each side of it till one lays.
const entrance = rides.list()[ride].entrance;
expect(entrance !== null, "the design's entrance placed");
await ui.click("tools/Paths/Obj_Paths");
await until(() => ui.find("footpaths/route") !== undefined, 120, "the path panel");
await ui.toggle("footpaths/follow", false);
for (const [dc, dr] of [[0, 1], [1, 0], [0, -1], [-1, 0]]) {
    if (rides.list()[ride].joined)
        break;
    const col = entrance!.col + dc;
    const r = entrance!.row + dr;
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

// Tested, its train runs off the end of the track and crashes.
const refused = rides.setState(ride, "testing");
expect(refused == null, `the open track tested, not refused with ${refused}`);
await ui.click("bar/Top_SuperFastForward");
// The design's track is long and its train slow round it: the park's clock runs at most a few times faster
// than the frames.
await until(() => (rides.list()[ride].crashes ?? 0) > 0, 20000, "the train crashed");
// Back to the game's own pace, so the wrecks are seen in flight.
await ui.click("bar/Top_Play");
const flying = rides.list()[ride].wrecks ?? [];
expect(flying.length === (rides.list()[ride].carsPerTrain ?? 0), "a wreck for each car");
const first = flying[0];
camera.look(first, { col: first.col + 6, row: first.row + 14, up: (first.up ?? 0) + 8 });
await frames(10);
await screenshot("flying");
await until(() => (rides.list()[ride].wrecks ?? []).some((w) => w.state === "burning"), 1200, "a wreck blown up");
const burning = (rides.list()[ride].wrecks ?? []).find((w) => w.state === "burning")!;
camera.look(burning, { col: burning.col + 6, row: burning.row + 14, up: (burning.up ?? 0) + 8 });
await frames(20);
await screenshot("burning");
expect(rides.list()[ride].open === false, "the crashed ride closed");
await until(() => (rides.list()[ride].wrecks ?? []).length === 0, 3600, "the wrecks gone");
await screenshot("cleared");
