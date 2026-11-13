// SPDX-License-Identifier: GPL-3.0-or-later
//
// A path joins a ride's exit only from behind it, the side away from the ride, and the ride takes it at once,
// even while guests queue for it. On the game's blank landscape a small flat ride is built with its entrance on
// a road and its exit loose, opened, and guests are dropped until some queue. Then:
//
//   - a path laid past the exit's side, across its fence, does not join it;
//   - a path laid behind it, in a style other than the first, joins it while the queue stands, its riders come
//     out onto it, and the exit's tile is paved in that path's style;
//   - the exit moved, the ride running, to a tile with a path already behind it lets riders out there.
//
//   ./nob script scripts/tests/ride_exit_path.ts -- --no-audio
//
//          ride  X b       X exit, b path behind it, s path past its side
//          E     s b       E entrance
//          |     | |
//   =====================  road

export {};

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "the tool row and the build lists");
const gate = map.paths().reduce((a, b) => (b.row > a.row ? b : a));

// The smallest ride at least three tiles long, on dry land, so its exit can move along its side.
const item = build
    .flatRides()!
    .filter((i) => !i.water && i.doors && i.length >= 3)
    .sort((a, b) => a.width * a.length - b.width * b.length)[0];
expect(item !== undefined, "a small flat ride to build");
const col = gate.col + 4;
const first = gate.row + 4;
const last = first + item.length - 1;
const right = col + item.width - 1;
const road = last + 4;

/** Clicks tile (c, r) for the placing step `step`. */
async function place(step: string, c: number, r: number): Promise<void> {
    pointer.move(...on_view(c, r));
    await frames(2);
    expect(build.placing()?.step === step && build.placing()?.problem === "none", `${step} to place at ${c},${r}, not ${JSON.stringify(build.placing())}`);
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
const exit = rides.list()[ride].exit!;
log("ride", item.symbol, "exit", JSON.stringify(exit));
expect(exit.col === right + 1 && exit.row === last, `the exit east of the ride's last row, not ${JSON.stringify(exit)}`);

// Paths: the road from the gate, the way to the entrance.
await ui.click("tools/Paths/Obj_Paths");
await until(() => ui.find("footpaths/route") !== undefined, 120, "the path panel");
await ui.toggle("footpaths/follow", false);
await ui.click("footpaths/queues");
await frames(2);
const queues = ui.children("footpaths/body") ?? [];
await ui.click("footpaths/paths");
await frames(2);
// The path styles: the buttons on the paths page that the queues page lacks.
const styles = (ui.children("footpaths/body") ?? []).filter((n) => !queues.includes(n));
const plain = styles.find((s) => s.toLowerCase().includes("asphalt")) ?? styles[0];
await ui.click(`footpaths/body/${plain}`);
await frames(2);
async function route(a: [number, number], b: [number, number]): Promise<void> {
    await look_down((a[0] + b[0]) >> 1, (a[1] + b[1]) >> 1, Math.max(60, 17 * (Math.abs(a[0] - b[0]) / 2 + 1), 8 * (Math.abs(a[1] - b[1]) / 2 + 1)));
    await ui.toggle("footpaths/route", false);
    await pointer.click(...on_view(...a));
    await ui.toggle("footpaths/route", true);
    for (let wait = 1; wait <= 96 && !has_path(...b); wait *= 2) {
        pointer.move(...on_view(...b));
        await frames(wait);
        await pointer.click(...on_view(...b));
    }
    await ui.toggle("footpaths/route", false);
    expect(has_path(...a) && has_path(...b), `a path from ${a} to ${b}, laid ${JSON.stringify(map.paths().filter((p) => Math.abs(p.col - a[0]) + Math.abs(p.row - a[1]) < 12).map((p) => [p.col, p.row]))}`);
}
await route([gate.col, gate.row + 1], [gate.col, road]);
await route([gate.col + 1, road], [right + 4, road]);
await route([col, last + 2], [col, road - 1]);

const why = rides.setState(ride, "open");
expect(why == null, `the ride opened, not kept closed by ${why}`);
await park.advanceUntil(() => rides.list()[ride].joined === true, 10, "the ride run by the park");
park.dropParties(60, 4);
await park.advanceUntil(() => (rides.list()[ride].queue ?? 0) > 0, 120, "guests queueing for the ride");
expect(rides.list()[ride].exitAt === undefined, "no way out yet");

// Past the exit's side: no join.
await route([right + 1, last + 1], [right + 1, road - 1]);
await frames(5);
log("side path", JSON.stringify(rides.list()[ride].exitAt));
expect(rides.list()[ride].exitAt === undefined, `a path past the exit's side not taken, not ${JSON.stringify(rides.list()[ride].exitAt)}`);

// Behind it, in another style, while guests queue: joined at once, and paved like it.
const other = must(styles.find((s) => s !== plain), "another path style");
await ui.click("footpaths/paths");
await until(() => ui.find(`footpaths/body/${other}`) !== undefined, 60, other);
await ui.click(`footpaths/body/${other}`);
await frames(2);
await route([right + 2, last], [right + 2, road - 1]);
await frames(5);
const queued = rides.list()[ride].queue ?? 0;
const out = rides.list()[ride].exitAt;
log("behind", JSON.stringify(out), "queue", queued);
expect(queued > 0, "guests still queueing");
expect(out?.col === right + 2 && out?.row === last, `the path behind the exit taken at once, not ${JSON.stringify(out)}`);
const style = map.paths().find((p) => p.col === right + 2 && p.row === last)!.style;
const paved = map.pavings().find((p) => p.col === exit.col && p.row === exit.row);
log("path style", style, "exit paved", JSON.stringify(paved));
expect(paved?.style === style, `the exit paved like the path behind it (${style}), not ${JSON.stringify(paved)}`);
const onTile = (c: number, r: number) => people.list().some((p) => !p.staff && p.col !== undefined && Math.floor(p.col) === c && Math.floor(p.row!) === r);
await park.advanceUntil(() => onTile(right + 2, last), 120, "a rider out onto the path behind the exit");
await look_down(right, last, 18);
await screenshot("exit_joined");

// Moved along the side, the ride running, to a tile with a path behind it already.
const along = last - 1;
await route([right + 2, along], [right + 2, along]);
// The path tool put away, so its ghost does not catch the pointer.
await ui.click("footpaths/close");
await frames(3);
rides.show(ride);
await frames(5);
await ui.click("info/Guest_Rides");
await frames(3);
await ui.click("info/body/Attraction_General_Exit");
await frames(3);
await look_down(right, along, 20);
await place("exit", right + 1, along);
await frames(5);
const moved = rides.list()[ride].exitAt;
log("moved", JSON.stringify(rides.list()[ride].exit), "out at", JSON.stringify(moved));
expect(moved?.col === right + 2 && moved?.row === along, `the moved exit's path taken, not ${JSON.stringify(moved)}`);
await screenshot("exit_moved");
quit(0);
