// SPDX-License-Identifier: GPL-3.0-or-later
//
// A queue joins a ride's entrance only from behind it, the tile away from the ride, at its height. On the game's
// blank landscape a small flat ride is built with its entrance below it. Then:
//
//   - a queue laid along the entrance's side does not join the ride's line;
//   - pointed at near the entrance's side edge, the path tool offers the next piece out of its back, and its ghost
//     shows the queue turned to lead into the entrance, as it is laid;
//   - laid there, the queue joins the ride's line;
//   - a queue coming along the row behind a second ride's entrance: the ghost of its piece behind the entrance turns
//     into it, as the piece laid there does.
//
//   ./nob script scripts/tests/queue_entrance.ts -- --no-audio
//
//          ride        E entrance, its back facing the higher rows
//          E s s s     s queue along its side, not joined
//          b           b queue behind it, joined

export {};

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(
    () => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined,
    1200,
    "the tool row and the build lists",
);
const gate = map.paths().reduce((a, b) => (b.row > a.row ? b : a));
const item = build
    .flatRides()!
    .filter((i) => !i.water && i.doors)
    .sort((a, b) => a.width * a.length - b.width * b.length)[0];
expect(item !== undefined, "a small flat ride to build");
const col = gate.col + 4;
const first = gate.row + 4;
const last = first + item.length - 1;
const right = col + item.width - 1;

/** Clicks tile (c, r) for the placing step `step`. */
async function place(step: string, c: number, r: number): Promise<void> {
    pointer.move(...on_view(c, r));
    await frames(2);
    expect(
        build.placing()?.step === step && build.placing()?.problem === "none",
        `${step} to place at ${c},${r}, not ${JSON.stringify(build.placing())}`,
    );
    await pointer.click(...on_view(c, r));
    await frames(2);
}

await look_down(col + 1, last, 40);
expect(build.pick(item.overlay, item.symbol), `${item.symbol} picked`);
await place("ride", col + ((item.width - 1) >> 1), first + ((item.length - 1) >> 1));
await place("entrance", col, last + 1);
await place("exit", right + 1, last);
await ui.click("info/close");
const ride = rides.list().length - 1;
const door = must(rides.list()[ride].entrance, "the ride's entrance");
log("ride", item.symbol, "entrance", JSON.stringify(door));
expect(door.col === col && door.row === last + 1, `the entrance below the ride, not ${JSON.stringify(door)}`);

await ui.click("tools/Paths/Obj_Paths");
await until(() => ui.find("footpaths/route") !== undefined, 120, "the path panel");
await ui.toggle("footpaths/follow", false);
await ui.click("footpaths/paths");
await frames(2);
const paths = ui.children("footpaths/body") ?? [];
await ui.click("footpaths/queues");
await frames(2);
const queues = (ui.children("footpaths/body") ?? []).filter((n) => !paths.includes(n));
const style = must(queues.find((n) => n.toLowerCase().includes("space")) ?? queues[0], "a queue style");
await ui.click(`footpaths/body/${style}`);
await frames(2);
const queued = (c: number, r: number) => map.paths().some((p) => p.col === c && p.row === r && p.queue);
/** The line of the queue on tile (c, r), if it is on one. */
const line = (c: number, r: number) => map.paths().find((p) => p.col === c && p.row === r && p.queue)?.line;

// Along the entrance's side: not joined.
await look_down(col + 1, last + 1, 25);
await ui.toggle("footpaths/route", false);
await pointer.click(...on_view(col + 1, last + 1));
await ui.toggle("footpaths/route", true);
for (let wait = 1; wait <= 96 && !queued(col + 3, last + 1); wait *= 2) {
    pointer.move(...on_view(col + 3, last + 1));
    await frames(wait);
    await pointer.click(...on_view(col + 3, last + 1));
}
await ui.toggle("footpaths/route", false);
expect(queued(col + 1, last + 1) && queued(col + 3, last + 1), "a queue laid along the entrance's side");
log("side queue lines", line(col + 1, last + 1), line(col + 3, last + 1));
expect(line(col + 1, last + 1) === undefined, "a queue along the entrance's side not joined to the ride");
await screenshot("queue_beside_entrance");

// The tool put away and taken out again, so no trail goes on and the entrance can be pointed at.
await ui.click("footpaths/close");
await frames(3);
await ui.click("tools/Paths/Obj_Paths");
await until(() => ui.find("footpaths/route") !== undefined, 120, "the path panel again");
await ui.toggle("footpaths/route", false);
await ui.click("footpaths/queues");
await until(() => ui.find(`footpaths/body/${style}`) !== undefined, 60, style);
await ui.click(`footpaths/body/${style}`);
await frames(2);

// Near the entrance's side edge, towards the queue along it: the next piece is still out of its back.
const near = must(map.screen(col + 0.9, last + 1.5), "the entrance's side edge in sight");
pointer.move(near.x, near.y);
await frames(5);
await screenshot("queue_ghost_at_entrance");
await pointer.click(near.x, near.y);
await frames(2);
await pointer.click(near.x, near.y);
await frames(5);
log(
    "queues",
    JSON.stringify(
        map
            .paths()
            .filter((p) => p.queue)
            .map((p) => [p.col, p.row, p.line]),
    ),
);
expect(queued(col, last + 2), `a queue laid behind the entrance, on ${col},${last + 2}`);
expect(!queued(col + 1, last + 2), "nothing laid out of the entrance's side");
log("behind queue line", line(col, last + 2));
expect(line(col, last + 2) !== undefined, "the queue behind the entrance joined to the ride");
pointer.move(0.5, 0.95);
await frames(5);
await screenshot("queue_laid_at_entrance");

// A second ride, reached by a queue coming along the row behind its entrance: the ghost of the piece behind the
// entrance already turns into it, as the piece does once laid.
await ui.click("footpaths/close");
await frames(3);
const col2 = right + 6;
const right2 = col2 + item.width - 1;
await look_down(col2 + 1, last, 40);
expect(build.pick(item.overlay, item.symbol), `${item.symbol} picked again`);
await place("ride", col2 + ((item.width - 1) >> 1), first + ((item.length - 1) >> 1));
await place("entrance", col2, last + 1);
await place("exit", right2 + 1, last);
await ui.click("info/close");
await ui.click("tools/Paths/Obj_Paths");
await until(() => ui.find("footpaths/route") !== undefined, 120, "the path panel a third time");
await ui.toggle("footpaths/route", false);
await ui.click("footpaths/queues");
await until(() => ui.find(`footpaths/body/${style}`) !== undefined, 60, style);
await ui.click(`footpaths/body/${style}`);
await frames(2);
await look_down(col2 + 1, last + 2, 25);
await pointer.click(...on_view(col2 + 3, last + 2));
await ui.toggle("footpaths/route", true);
for (let wait = 1; wait <= 96 && !queued(col2 + 1, last + 2); wait *= 2) {
    pointer.move(...on_view(col2 + 1, last + 2));
    await frames(wait);
    await pointer.click(...on_view(col2 + 1, last + 2));
}
await ui.toggle("footpaths/route", false);
expect(queued(col2 + 1, last + 2), "a queue along the row behind the second entrance");
pointer.move(...on_view(col2, last + 2));
await frames(5);
await screenshot("queue_turn_ghost_at_entrance");
for (let i = 0; i < 4 && !queued(col2, last + 2); ++i) {
    await pointer.click(...on_view(col2, last + 2));
    await frames(3);
}
expect(queued(col2, last + 2), "the queue laid on behind the second entrance");
log("second queue line", line(col2, last + 2), line(col2 + 1, last + 2));
expect(line(col2, last + 2) !== undefined, "the queue behind the second entrance joined to its ride");
pointer.move(0.5, 0.95);
await frames(5);
await screenshot("queue_turn_laid_at_entrance");
quit(0);
