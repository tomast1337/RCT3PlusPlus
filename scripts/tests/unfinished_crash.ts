// SPDX-License-Identifier: GPL-3.0-or-later
//
// Crashes unfinished coasters on the game's blank landscape. A saved corkscrew design with a piece taken out, its
// entrance given a path, opened with guests about: they board, the train runs off the gap, and its riders are thrown
// out, fly and land knocked over. Then a track begun by hand, a station and a few straights, with no train of its
// own, tested and left: the park gives it a train, which runs off the end and crashes.
//
//   ./nob script scripts/tests/unfinished_crash.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

async function designs(): Promise<void> {
    expect(build.pick("tracks/coasters/Corkscrew/Corkscrew", "Corkscrew", "designs"), "a corkscrew coaster to build");
    await until(() => ui.find("design_browser") !== undefined, 600, "the designs window");
    await frames(3);
}

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "tools");
// The end of the path in from the park's gate farthest into the park.
const gate = map.paths().reduce((a, b) => (b.row > a.row ? b : a));

/** Where the middle of tile (col, row) is on the view; throws when it is out of sight. */
function on_view(col: number, row: number): [number, number] {
    const at = map.screen(col, row);
    if (!at)
        throw new Error(`tile ${col}, ${row} is not in sight`);
    return [at.x, at.y];
}

// A design with a gap, opened with guests about.
await designs();
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

const entrance = rides.list()[ride].entrance;
expect(entrance !== null, "the design's entrance placed");
await ui.click("tools/Paths/Obj_Paths");
await until(() => ui.find("footpaths/route") !== undefined, 120, "the path panel");
await ui.toggle("footpaths/follow", false);
let door: [number, number] | undefined;
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
    door = [col, r];
}
await until(() => rides.list()[ride].joined, 300, "the ride run by the park");
// A path on from the gate to the ride's, for guests to walk there.
const laid = () => map.paths().some((p) => p.col === gate.col && p.row === gate.row + 1);
const middle = { col: (gate.col + door![0]) / 2 + 0.5, row: (gate.row + door![1]) / 2 + 0.5 };
const up = Math.max(60, 17 * (Math.abs(gate.col - door![0]) / 2 + 1), 8 * (Math.abs(gate.row - door![1]) / 2 + 1));
camera.look(middle, { col: middle.col, row: middle.row + up / 3, up });
await frames(5);
await ui.toggle("footpaths/route", false);
await pointer.click(...on_view(door![0], door![1]));
await ui.toggle("footpaths/route", true);
for (let wait = 1; wait <= 96 && !laid(); wait *= 2) {
    pointer.move(...on_view(gate.col, gate.row + 1));
    await frames(wait);
    await pointer.click(...on_view(gate.col, gate.row + 1));
}
await ui.toggle("footpaths/route", false);
expect(laid(), "a path from the gate to the ride");
build.stop();
await ui.click("placing/close");
await frames(5);

// Opened unfinished, guests board it.
const refused = rides.setState(ride, "open");
expect(refused == null, `the unfinished coaster opened, not refused with ${refused}`);
const drops = park.dropParties(40);
log("dropped", drops.reduce((n, p) => n + p.guests, 0), "guests on the paths");
await ui.click("bar/Top_SuperFastForward");
// Watched till its train sets off with riders aboard.
await until(() => (rides.list()[ride].riders ?? 0) > 0 && (rides.list()[ride].moving ?? 0) > 0, 20000, "the unfinished coaster under way with riders");
log("aboard:", rides.list()[ride].riders);
await until(() => (rides.list()[ride].crashes ?? 0) > 0, 20000, "the train with riders crashed");
await ui.click("bar/Top_Play");

// Its riders fly out, land knocked over and get up.
const thrown = () => people.list().filter((p) => p.state === "thrown");
expect(thrown().length > 0, "riders thrown out");
const car = (rides.list()[ride].wrecks ?? [])[0];
camera.look(car, { col: car.col + 5, row: car.row + 12, up: (car.up ?? 0) + 2 });
await frames(6);
expect(thrown().some((p) => (p.up ?? 0) > 2), "riders in the air");
await screenshot("riders_flying");
await until(() => thrown().length === 0, 1200, "the thrown riders landed");
const lying = people.list().filter((p) => p.state === "dropped");
expect(lying.length > 0, "riders lying where they landed");
log("landed at heights", lying.map((p) => (p.up ?? 0).toFixed(1)).join(" "));
await frames(30);
await screenshot("landed");
expect(rides.list()[ride].open === false, "the crashed ride closed");

// Begun by hand well away from the design: the custom track's first piece, its station, and straights on from it.
camera.look({ col: entrance!.col - 30.5, row: entrance!.row + 0.5 }, { col: entrance!.col - 30.5, row: entrance!.row + 16.5, up: 30 });
await frames(5);
await designs();
await ui.click("design_browser/content/build");
await frames(10);
pointer.move(0.3, 0.4);
await frames(10);
await ui.click("placing/body/construction/special0");
await frames(5);
expect(/station/i.test(build.construction()?.piece ?? ""), "a station picked to begin with");
pointer.move(0.3, 0.55);
await frames(10);
await pointer.click(0.3, 0.55);
await frames(20);
const begun = build.construction()!.ride;
expect(begun !== ride && rides.list()[begun].pieces === 1, "the first piece laid");
for (let i = 0; i < 4; ++i) {
    await ui.click("placing/body/construction/AttractionEditor_IconCurve0");
    await frames(10);
}
log("laid by hand:", rides.list()[begun].pieces, "pieces");
expect((rides.list()[begun].pieces ?? 0) > 1, "straights laid on from the station");
const untested = rides.setState(begun, "testing");
expect(untested == null, `the track begun by hand tested, not refused with ${untested}`);
await frames(10);
expect(build.construction()?.testing === true, "a ghost test train while the panel has it");
build.stop();
await ui.click("placing/close");
await frames(10);
await until(() => (rides.list()[begun].running ?? 0) > 0, 120, "its own train run once the panel is closed");
await ui.click("bar/Top_SuperFastForward");
await until(() => (rides.list()[begun].crashes ?? 0) > 0, 6000, "the train begun by hand crashed");
await ui.click("bar/Top_Play");
const wreck = (rides.list()[begun].wrecks ?? [])[0];
expect(wreck !== undefined, "its wrecks flying");
camera.look(wreck, { col: wreck.col + 6, row: wreck.row + 14, up: (wreck.up ?? 0) + 8 });
await frames(10);
await screenshot("hand_built_crash");
expect(rides.list()[begun].open === false, "the crashed ride closed");
