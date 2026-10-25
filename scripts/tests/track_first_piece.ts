// SPDX-License-Identifier: GPL-3.0-or-later
//
// Begins a custom corkscrew coaster track on the game's blank landscape. With Shift held the first piece stays
// on its tile and rises or sinks with the pointer, never under the ground; let go, it keeps its height while it
// follows the pointer, and is laid there. Demolishing it again gives back the first piece to place anywhere.
//
//   ./nob script scripts/tests/track_first_piece.ts -- --no-audio

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
await ui.click("design_browser/content/build");
await frames(10);
pointer.move(0.5, 0.45);
await frames(10);
const start = build.construction()!.ghostAt!;

pointer.shift(true);
await frames(3);
pointer.move(0.5, 0.25);
await frames(5);
const raised = build.construction()!.ghostAt!;
expect(raised.height > start.height && raised.col === start.col && raised.row === start.row, "the first piece raised on its tile");
await screenshot("raised");
pointer.move(0.5, 0.6);
await frames(5);
expect(build.construction()!.ghostAt!.height === start.height, "the first piece sunk no lower than the ground");
pointer.move(0.5, 0.3);
await frames(5);
const lifted = build.construction()!.ghostAt!.height;
pointer.shift(false);
await frames(3);
pointer.move(0.4, 0.5);
await frames(5);
const moved = build.construction()!.ghostAt!;
expect(moved.height === lifted && (moved.col !== start.col || moved.row !== start.row), "the height kept as the piece follows the pointer");
await pointer.click(0.4, 0.5);
await frames(20);
const ride = build.construction()!.ride;
expect(rides.list()[ride].pieces === 1, "the first piece laid");
await screenshot("laid_raised");

await ui.click("placing/body/construction/demolish");
await frames(10);
expect(rides.list()[ride].pieces === 0, "the first piece demolished");
pointer.move(0.35, 0.6);
await frames(10);
const free = build.construction()!.ghostAt!;
expect(free.height === start.height && (free.col !== moved.col || free.row !== moved.row), "the first piece placed anywhere again, on the ground");
await pointer.click(0.35, 0.6);
await frames(20);
expect(rides.list()[ride].pieces === 1, "the first piece laid again");
await screenshot("laid_again");
