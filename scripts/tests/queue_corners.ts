// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ground queue corners: a queue tile where the line turns shows its low walls along the two sides the line does
// not pass, open towards the tiles it comes from and goes to. A queue laid in a snake on the game's blank
// landscape turns both ways, and is looked at from above; then a saved park's queues, laid in the original game.
//
//   ./nob script scripts/tests/queue_corners.ts -- --no-audio
//
//   s---+       s start, + corners
//       |
//   +---+
//   |
//   +---e

export {};

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined, 1200, "the tool row");
const gate = map.paths().reduce((a, b) => (b.row > a.row ? b : a));
const col = gate.col + 4;
const row = gate.row + 4;

await ui.click("tools/Paths/Obj_Paths");
await until(() => ui.find("footpaths/route") !== undefined, 120, "the path panel");
await ui.toggle("footpaths/follow", false);
await ui.click("footpaths/paths");
await frames(2);
const paths = ui.children("footpaths/body") ?? [];
await ui.click("footpaths/queues");
await frames(2);
// The space queue's lanes show its walls clearly; any queue style does otherwise.
const queues = (ui.children("footpaths/body") ?? []).filter((n) => !paths.includes(n));
const style = must(queues.find((n) => n.toLowerCase().includes("space")) ?? queues[0], "a queue style");
await ui.click(`footpaths/body/${style}`);
await frames(2);

await look_down(col + 1, row + 2, 30);
const queued = (c: number, r: number) => map.paths().some((p) => p.col === c && p.row === r && p.queue);
await ui.toggle("footpaths/route", false);
await pointer.click(...on_view(col, row));
await ui.toggle("footpaths/route", true);
for (const [c, r] of [
    [col + 3, row],
    [col + 3, row + 2],
    [col, row + 2],
    [col, row + 4],
    [col + 3, row + 4],
] as const) {
    for (let wait = 1; wait <= 96 && !queued(c, r); wait *= 2) {
        pointer.move(...on_view(c, r));
        await frames(wait);
        await pointer.click(...on_view(c, r));
    }
    expect(queued(c, r), `a queue laid to ${c},${r}`);
}
await ui.toggle("footpaths/route", false);
await ui.click("footpaths/close");
await frames(3);
camera.look({ col: col + 2, row: row + 2.5 }, { col: col + 2, row: row + 2.6, up: 14 });
await frames(5);
await screenshot("snake_from_above");

// A saved park's queue, laid in the original game, from above.
await park.open("Campaigns/Base/TheMoneyPit.dat");
await park.ready();
expect(
    map.paths().some((p) => p.queue && p.col >= 33 && p.col <= 36 && p.row >= 17 && p.row <= 20),
    "the saved park's queue by tiles 33..36, 17..20",
);
camera.look({ col: 35, row: 19 }, { col: 35, row: 19.1, up: 14 });
await frames(10);
await screenshot("saved_from_above");
quit(0);
