// SPDX-License-Identifier: GPL-3.0-or-later
//
// Builds a ride of two tracks side by side on the game's blank landscape: its first piece, a joined station,
// lays both halves; the split button then steps from joined pieces to the left track alone, joined pieces
// from the right track's end and the right track alone, and a piece laid alone goes on one track only.
//
//   ./nob script scripts/tests/split_track_test.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "tools");
expect(build.pick("tracks/coasters/Seizmic/Seizmic", "seizmic"), "a ride of two tracks to build");
await frames(5);
expect(build.construction()?.split === 1, "joined pieces from the left track first");
const ride = build.construction()!.ride;
// Close over the middle of the park, the station laid on the tile there.
const { width, height } = map.size();
const col = Math.floor(width / 2);
const row = Math.floor(height / 2);
camera.look({ col: col + 0.5, row: row + 0.5 }, { col: col + 0.5, row: row + 6.5, up: 15 });
await frames(5);
const at = map.screen(col, row)!;
pointer.move(at.x, at.y);
await frames(30);
await pointer.click(at.x, at.y);
await frames(30);
expect(rides.list()[ride].pieces === 2, "both halves of the station laid");
await frames(60);
await screenshot("station");

await ui.click("placing/body/construction/split");
await frames(5);
expect(build.construction()?.split === 2, "the left track alone next");
expect(build.construction()?.ghost === true, "a piece of the left track alone offered");
await ui.click("placing/body/construction/AttractionEditor_IconCurve0");
await frames(10);
expect(rides.list()[ride].pieces === 3, "a piece laid on the left track only");
expect(rides.list()[ride].track?.at(-1)?.endsWith("_l:tks") === true, "the piece laid of the left track alone");
camera.look({ col: col + 0.5, row: row + 0.5 }, { col: col + 0.5, row: row + 6.5, up: 15 });
await frames(60);
await screenshot("left_alone");

await ui.click("placing/body/construction/split");
await frames(5);
expect(build.construction()?.split === 3, "joined pieces from the right track's end next");
await ui.click("placing/body/construction/split");
await frames(5);
expect(build.construction()?.split === 4, "the right track alone next");
expect(build.construction()?.ghost === true, "a piece of the right track alone offered");
await ui.click("placing/body/construction/split");
await frames(5);
expect(build.construction()?.split === 1, "back to joined pieces from the left");
