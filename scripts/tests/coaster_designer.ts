// SPDX-License-Identifier: GPL-3.0-or-later
//
// The coaster designer: its park offers coasters only; a design picked there is laid with its track open for
// building on; the ride's window saves its design in the game's format, named "[Kind]Name"; and the design
// saved is then offered with the shipped ones, in the designer and in any park, and lays as it was.
//
//   ./nob script scripts/tests/coaster_designer.ts -- --no-intro --no-audio --seed 1

export {};

const kOverlay = "tracks/coasters/Corkscrew/Corkscrew";
const kKind = "Corkscrew";
const kName = "Designer Loop";

/** Picks the saved design of the kind whose file names `design` (the first when none), leaving it in hand. */
async function pickDesign(design?: string): Promise<RideIndex> {
    expect(build.pick(kOverlay, kKind, "designs"), `a ${kKind} to build`);
    await until(() => ui.find("design_browser") !== undefined, 600, "the designs window");
    await frames(3);
    const row = design === undefined ? 0 : build.construction()!.designs.findIndex((d) => d.includes(design));
    expect(row >= 0, `the ${design} design among ${build.construction()!.designs.join(", ")}`);
    const list = must(ui.find("design_browser/content/list"), "design_browser/content/list shown");
    const button = must(ui.find("design_browser/content/build"), "design_browser/content/build shown");
    const unit = (button.bottom - button.top) / 22;
    await pointer.click(list.x, list.top + (18 + 16 * (row + 1) + 8) * unit);
    await frames(5);
    await ui.click("design_browser/content/build");
    await frames(10);
    return build.construction()!.ride as RideIndex;
}

/** Lays the design in hand round the middle of the map. */
async function layInMiddle(): Promise<void> {
    const { width, height } = map.size();
    const middle = { col: Math.floor(width / 2), row: Math.floor(height / 2) };
    camera.look(middle, { col: middle.col, row: middle.row + 30, up: 45 });
    await frames(4);
    const at = must(map.screen(middle.col, middle.row), "the middle in sight");
    pointer.move(at.x, at.y);
    await frames(4);
    expect(build.construction()?.refusal === "", `the design free, not ${build.construction()?.refusal}`);
    await pointer.click(at.x, at.y);
    await frames(30);
}

// ── 1. The designer's park: coasters only, a design laid opens its track for building ──
await park.open("Campaigns/Base/CoasterDesigner.dat", { designer: true });
await until(() => park.loaded() && build.flatRides() !== undefined, 1200, "the designer's park");
await frames(10);
const flat = build.flatRides()!.find((r) => build.offers(r.symbol) !== undefined);
log("a flat ride", flat?.symbol, "offered:", flat ? build.offers(flat.symbol) : "-");
const ride = await pickDesign();
await layInMiddle();
const laid = tracked(rides.list()[ride], "the design laid");
expect(laid.pieces > 0, "the design laid");
await until(() => build.construction()?.ride === ride, 120, "its track open for building on");
await screenshot("coaster_designer_built");
const pieces = laid.track;

// Tested, its window open with the camera that rides it.
await ui.click("placing/close");
await frames(5);
const refused = rides.setState(ride, "testing");
expect(!refused, `a test started, not ${refused}`);
await frames(600);
rides.show(ride);
await until(() => ui.find("info/body/File_SaveGame") !== undefined, 120, "the save design button");
await ui.click("info/body/RideCamGUI_Enabled");
await frames(30);
await screenshot("coaster_designer_cam");
keys.press("Escape");
await frames(10);
rides.setState(ride, "closed");
await frames(10);

// ── 2. Saved from its window, under the name the game gives a design ──
rides.show(ride);
await until(() => ui.find("info/body/File_SaveGame") !== undefined, 120, "the save design button");
await ui.click("info/body/File_SaveGame");
await until(() => ui.find("design_files") !== undefined, 120, "the save window");
const offered = ui.find("design_files/content/file_name")?.text ?? "";
log("offered name", offered);
expect(offered.startsWith(`[${kKind}]`), `the kind before the name, not "${offered}"`);
await ui.click("design_files/content/file_name");
keys.press("Ctrl+A");
keys.type(`[${kKind}]${kName}`);
await frames(3);
await screenshot("coaster_designer_save");
await ui.click("design_files/content/open");
await until(() => ui.find("design_files") === undefined, 120, "the save window to close");
await frames(30);

// ── 3. Offered with the shipped designs in another park, and laid as it was saved ──
await park.open("Campaigns/Base/BlankLandscape.dat", { sandbox: true });
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "tools");
expect(build.pick(kOverlay, kKind, "designs"), `a ${kKind} to build`);
await until(() => ui.find("design_browser") !== undefined, 600, "the designs window");
await frames(5);
const designs = build.construction()!.designs;
log("designs", designs.join(", "));
expect(
    designs.some((d) => d.includes(kName)),
    `the saved design among ${designs.join(", ")}`,
);
await screenshot("coaster_designer_picker");
await ui.click("design_browser/close");
await frames(5);
const again = await pickDesign(kName);
await layInMiddle();
const back = tracked(rides.list()[again], "the saved design laid");
expect(
    JSON.stringify(back.track) === JSON.stringify(pieces),
    `the same track back, ${back.track.length} pieces of ${pieces.length}`,
);
await screenshot("coaster_designer_placed");
