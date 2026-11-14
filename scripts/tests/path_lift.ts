// SPDX-License-Identifier: GPL-3.0-or-later
//
// Lifting a path with Shift: with Shift down, moving the pointer up raises the next piece's ghost off the ground,
// deck and supports, its walking surface showing. A click lays it there, and the trail goes on from it: with Shift
// still down the next piece is the one beside it at its height, wherever the pointer is, and Straight lays that
// one. Ghosts cast no shadows.
//
//   ./nob script scripts/tests/path_lift.ts -- --no-audio

export {};

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined, 1200, "the tool row");
const gate = map.paths().reduce((a, b) => (b.row > a.row ? b : a));
const col = gate.col + 3;
const row = gate.row + 4;

await ui.click("tools/Paths/Obj_Paths");
await until(() => ui.find("footpaths/route") !== undefined, 120, "the path panel");
await ui.toggle("footpaths/follow", false);
await ui.toggle("footpaths/route", false);
await ui.click("footpaths/paths");
await frames(2);
const styles = ui.children("footpaths/body") ?? [];
const asphalt = styles.find((s) => s.toLowerCase().includes("asphalt")) ?? styles[0];
await ui.click(`footpaths/body/${asphalt}`);
await frames(2);
const straight = must(under("footpaths", "Path_Straight"), "the Straight button");

await look_down(col, row, 30);
const [x, y] = on_view(col, row);
pointer.move(x, y);
await frames(2);
pointer.shift(true);
await frames(2);
for (let i = 1; i <= 16; ++i) {
    pointer.move(x, y - 0.01 * i);
    await frames(1);
}
await frames(3);
const ghost = build.placing();
log("lifted ghost", JSON.stringify(ghost));
await screenshot("ghost_lifted");

const before = map.paths().length;
await pointer.click(x, y - 0.16);
await frames(3);
const first = map.paths().slice(before);
log("first", JSON.stringify(first));
expect(first.length === 1 && first[0].raised, `one raised piece laid, not ${JSON.stringify(first)}`);
await frames(3);
await screenshot("first_laid_shift_held");
await ui.click(straight);
await frames(3);
pointer.shift(false);
await frames(2);
const laid = map.paths().slice(before);
log("laid", JSON.stringify(laid));
expect(laid.length === 2, `two pieces laid, not ${JSON.stringify(laid)}`);
const [a, b] = laid;
expect(b.raised && b.height === a.height, `the second piece raised at the first's height, not ${JSON.stringify(laid)}`);
expect(
    Math.abs(a.col - b.col) + Math.abs(a.row - b.row) === 1,
    `the second piece beside the first, not ${JSON.stringify(laid)}`,
);
await screenshot("two_laid");
quit(0);
