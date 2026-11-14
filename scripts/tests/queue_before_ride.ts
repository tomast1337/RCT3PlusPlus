// SPDX-License-Identifier: GPL-3.0-or-later
//
// A queue laid before its ride joins the ride's line once the entrance is put in front of it. On the game's blank
// landscape a straight queue is laid first, then a small flat ride whose entrance's back meets the queue's end:
//
//   - every tile of the queue takes the ride's line, as when it is laid after the entrance;
//   - the ride demolished, the queue goes back to a plain queue on no line.
//
//   ./nob script scripts/tests/queue_before_ride.ts -- --no-audio
//
//          ride        E entrance, its back facing the higher rows
//          E           q queue laid before the ride, away from where the entrance goes
//          q
//          q
//          q

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
const rows = [last + 2, last + 3, last + 4, last + 5];

const queued = (c: number, r: number) => map.paths().some((p) => p.col === c && p.row === r && p.queue);
/** The line of the queue on tile (c, r), if it is on one. */
const line = (c: number, r: number) => map.paths().find((p) => p.col === c && p.row === r && p.queue)?.line;

// The queue first, from behind where the entrance will stand, away from it.
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
await look_down(col, last + 3, 25);
await ui.toggle("footpaths/route", false);
await pointer.click(...on_view(col, rows[0]));
await ui.toggle("footpaths/route", true);
for (let wait = 1; wait <= 96 && !queued(col, rows[3]); wait *= 2) {
    pointer.move(...on_view(col, rows[3]));
    await frames(wait);
    await pointer.click(...on_view(col, rows[3]));
}
await ui.toggle("footpaths/route", false);
await ui.click("footpaths/close");
await frames(3);
expect(
    rows.every((r) => queued(col, r) && line(col, r) === undefined),
    "a queue on no line laid before the ride",
);

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

// Then the ride, its entrance's back on the queue's end.
await look_down(col + 1, last + 1, 40);
expect(build.pick(item.overlay, item.symbol), `${item.symbol} picked`);
await place("ride", col + ((item.width - 1) >> 1), first + ((item.length - 1) >> 1));
await place("entrance", col, last + 1);
await place("exit", right + 1, last);
await ui.click("info/close");
const ride = rides.list().length - 1;
const door = must(rides.list()[ride].entrance, "the ride's entrance");
expect(door.col === col && door.row === last + 1, `the entrance in front of the queue, not ${JSON.stringify(door)}`);
pointer.move(0.5, 0.95);
await frames(10);
await screenshot("queue_before_ride");
const lines = rows.map((r) => line(col, r));
log("queue lines", JSON.stringify(lines));
expect(
    lines[0] !== undefined && lines.every((l) => l === lines[0]),
    `the whole queue on the ride's line, not ${JSON.stringify(lines)}`,
);

// The ride pulled down: its queue goes back to a plain one.
rides.show(ride);
await frames(10);
await ui.click("info/body/Attraction_General_Delete");
await frames(2);
await ui.click("demolish_question/content/GUI2MiniDialog_Confirm");
await frames(10);
expect(rides.list()[ride].demolished, "the ride pulled down");
expect(
    rows.every((r) => queued(col, r) && line(col, r) === undefined),
    "its queue on no line once the ride is gone",
);
await screenshot("queue_after_demolish");
quit(0);
