// SPDX-License-Identifier: GPL-3.0-or-later
//
// Places a saved corkscrew design on the game's blank landscape. Its ghost shows the design's entrance and exit
// with its track, and the whole design is laid with its doors. The same design again over the first is refused
// whole, its ghost tinted and nothing laid; moved onto clear ground it is laid whole, doors and all.
//
//   ./nob script scripts/tests/design_place.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "tools");

// Picks the first saved corkscrew design in its window and builds it, leaving its ghost under the pointer.
async function pickDesign(): Promise<number> {
    expect(build.pick("tracks/coasters/Corkscrew/Corkscrew", "Corkscrew", "designs"), "a corkscrew coaster to build");
    await until(() => ui.find("design_browser") !== undefined, 600, "the designs window");
    await frames(3);
    const list = ui.find("design_browser/content/list")!;
    const button = ui.find("design_browser/content/build")!;
    const unit = (button.bottom - button.top) / 22;
    await pointer.click(list.x, list.top + (18 + 16 * 2) * unit);
    await frames(5);
    await ui.click("design_browser/content/build");
    await frames(10);
    expect(ui.find("design_browser") === undefined, "the window closed by Build");
    return build.construction()!.ride;
}

const first = await pickDesign();
pointer.move(0.5, 0.45);
await frames(30);
const ghost = build.construction()!;
expect(ghost.ghost && !ghost.clashes, `a ghost that fits on clear ground, not ${JSON.stringify(ghost)}`);
expect(ghost.ghostDoors === 2, `the entrance and exit in the ghost, not ${ghost.ghostDoors}`);
await screenshot("ghost_doors");
// Nearer, round the station.
pointer.wheel(6);
await frames(30);
pointer.move(0.5, 0.45);
await frames(10);
expect(build.construction()!.ghostDoors === 2, "the doors in the ghost seen nearer");
await screenshot("ghost_doors_near");
pointer.wheel(-6);
await frames(30);
pointer.move(0.5, 0.45);
await frames(10);
await pointer.click(0.5, 0.45);
await frames(30);
const laid = rides.list()[first];
expect((laid.pieces ?? 0) > 0 && laid.entrance !== null && laid.exit !== null, `the design laid with its doors, not ${JSON.stringify(laid)}`);
const length = laid.pieces!;
build.stop();
await frames(5);

// Over the first, refused whole.
const second = await pickDesign();
pointer.move(0.5, 0.45);
await frames(30);
const blocked = build.construction()!;
expect(blocked.ghost && blocked.clashes && blocked.ghostDoors === 2, `a refused ghost with its doors, not ${JSON.stringify(blocked)}`);
await screenshot("ghost_refused");
await pointer.click(0.5, 0.45);
await frames(30);
expect((rides.list()[second].pieces ?? 0) === 0, `nothing of a refused design laid, not ${rides.list()[second].pieces}`);
expect(build.construction()?.ride === second, "the refused design still in hand");

// Moved off the first, onto clear ground, laid whole.
let spot: [number, number] | undefined;
for (let i = 0; i < 60 && spot === undefined; ++i) {
    const at: [number, number] = [0.05 + 0.06 * (i % 15), 0.3 + 0.1 * Math.floor(i / 15)];
    pointer.move(...at);
    await frames(3);
    const c = build.construction()!;
    if (c.ghost && !c.clashes)
        spot = at;
}
expect(spot !== undefined, "clear ground for the second design");
await screenshot("ghost_clear");
await pointer.click(...spot!);
await frames(30);
const again = rides.list()[second];
expect(again.pieces === length && again.entrance !== null && again.exit !== null, `the second design laid whole with its doors, not ${JSON.stringify(again)}`);
await screenshot("laid_both");
