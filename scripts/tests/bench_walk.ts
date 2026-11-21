// SPDX-License-Identifier: GPL-3.0-or-later
//
// Worn-out guests walking to a bench, on the game's blank landscape: one bench partway along the road in from
// the entrance, parties dropped on the paths and worn out. Those with nowhere else to go walk along the road to it and
// sit down, never more than its seats.
//
//   ./nob script scripts/tests/bench_walk.ts -- --no-audio

export {};

await park.open("Campaigns/Base/BlankLandscape.dat");
await park.ready();
await until(() => build.flatRides() !== undefined, 1200, "the build lists");
const paths = map.paths();
const gate = paths.reduce((a, b) => (b.row > a.row ? b : a));
const road = paths.filter((p) => p.col === gate.col && p.row < gate.row - 1).sort((a, b) => a.row - b.row);
expect(road.length >= 3, `3 tiles of road at least, not ${road.length}`);
const end = must(
    road.find((p) => p.row === gate.row - 3),
    "the road 3 tiles in",
);
log("road tiles", road.length, "bench at row", end.row, "gate row", gate.row);

// The bench against the side of the road 3 tiles in, where no party is dropped.
camera.look({ col: end.col + 0.5, row: end.row + 0.5 }, { col: end.col + 0.5, row: end.row + 8.5, up: 20 });
await frames(3);
expect(build.pick("Style/Vanilla/Style", "Bench"), "the bench in the scenery lists");
await frames(2);
const at = must(map.screen(end.col + 0.1, end.row + 0.5), "the bench's tile in sight");
pointer.move(at.x, at.y);
await frames(3);
expect(build.placing()?.problem === "none", `the bench placeable, not ${build.placing()?.problem}`);
await pointer.click(at.x, at.y);
await frames(2);
build.stop();
await frames(2);
expect(park.pathItems().filter((i) => i.kind === "bench").length === 1, "one bench");

// Worn-out parties dropped about the paths make for it.
const dropped = park.dropParties(6, 3);
log("dropped at rows", dropped.map((d) => d.row).join(", "));
log("guests", park.tireGuests(0.1));
const sitters = () => people.list().filter((p): p is Person & { state: "sitting" } => p.state === "sitting");
let most = 0;
await park.advanceUntil(
    () => {
        most = Math.max(most, sitters().length);
        return sitters().length === 2;
    },
    600,
    "two guests sitting on the bench",
);
expect(most <= 2, `no more than two on the bench, not ${most}`);
log(
    "sitting",
    sitters()
        .map((p) => `${p.col.toFixed(2)},${p.row.toFixed(2)}`)
        .join("; "),
);
camera.look({ col: end.col + 0.5, row: end.row + 0.5, up: 0.5 }, { col: end.col + 0.9, row: end.row + 4, up: 4 });
await park.advance(3);
await frames(10);
await screenshot("bench_walk_sitting");
quit(0);
