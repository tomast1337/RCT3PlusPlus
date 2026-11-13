// SPDX-License-Identifier: GPL-3.0-or-later
//
// Guests on benches, on the game's blank landscape: benches put on both sides of the road in from the entrance,
// parties dropped on the paths and worn out. Their leaders sit down on the benches' seats, never more than a bench
// has, and two side by side are pictured talking; then, rested, they get up and walk on.
//
//   ./nob script scripts/tests/benches.ts -- --no-audio

export {};

await park.open("Campaigns/Base/BlankLandscape.dat");
await park.ready();
await until(() => build.flatRides() !== undefined, 1200, "the build lists");
const paths = map.paths();
const gate = paths.reduce((a, b) => (b.row > a.row ? b : a));
// Three tiles of the road in from the entrance, clear of the entrance itself.
const road = paths.filter((p) => p.col === gate.col && p.row < gate.row - 1).sort((a, b) => b.row - a.row).slice(0, 3);
expect(road.length === 3, `3 tiles of road, not ${road.length}`);
camera.look({ col: gate.col + 0.5, row: gate.row - 2.5 }, { col: gate.col + 0.5, row: gate.row + 6.5, up: 25 });
await frames(3);

// A bench against each side of each tile.
expect(build.pick("Style/Vanilla/Style", "Bench"), "the bench in the scenery lists");
await frames(2);
for (const tile of road)
    for (const side of [0.1, 0.9]) {
        const at = map.screen(tile.col + side, tile.row + 0.5);
        expect(at !== undefined, `tile ${tile.col},${tile.row} in sight`);
        pointer.move(at!.x, at!.y);
        await frames(3);
        expect(build.placing()?.problem === "none", `a bench placeable at ${tile.col + side},${tile.row}, not ${build.placing()?.problem}`);
        await pointer.click(at!.x, at!.y);
        await frames(2);
    }
build.stop();
await frames(2);
const benches = park.pathItems().filter((i) => i.kind === "bench");
expect(benches.length === 6, `6 benches, not ${benches.length}`);

// Parties on the paths, worn out: their leaders sit, two by two on each bench at most.
expect(park.dropParties(12, 5).length > 0, "parties dropped in the park");
log("guests", park.tireGuests(0.1));
const sitters = () => people.list().filter((p) => p.state === "sitting");
/** The sitters on each bench. */
const pairs = () => {
    const byBench = new Map<number, ReturnType<typeof sitters>>();
    for (const p of sitters())
        byBench.set(p.bench!, [...(byBench.get(p.bench!) ?? []), p]);
    return [...byBench.values()];
};
let most = 0;
await park.advanceUntil(() => {
    most = Math.max(most, ...pairs().map((b) => b.length), 0);
    return pairs().some((b) => b.length === 2 && b.every((p) => !p.rising));
}, 600, "two guests sitting side by side");
const pair = must(pairs().find((b) => b.length === 2), "two sitting on one bench");
log("sitting", sitters().length, "pair on seats", pair.map((p) => `${p.seat} at ${p.col!.toFixed(2)},${p.row!.toFixed(2)},${p.up!.toFixed(2)}`).join("; "), "road column", gate.col);
expect(new Set(pair.map((p) => p.seat)).size === 2, "the two on different seats");
expect(most <= 2, `no more than two on a bench, not ${most}`);

// Close up while they talk, a turn of their talk apart.
// Seen from above along the road, close by.
const middle = { col: (pair[0].col! + pair[1].col!) / 2, row: (pair[0].row! + pair[1].row!) / 2 };
camera.look({ ...middle, up: 0.5 }, { col: middle.col + 0.4, row: middle.row + 1.2, up: 3.5 });
await park.advance(3); // Sat down
await frames(10);
await screenshot("benches_pair");
await park.advance(4);
await frames(10);
await screenshot("benches_pair_later");

// Rested, they get up again.
await park.advanceUntil(() => people.list().some((p) => p.state === "sitting" && p.rising === true), 900, "a guest getting up");
await park.advanceUntil(() => sitters().length === 0 || people.list().filter((p) => p.state === "walking").length > 0, 120, "guests walking on");
quit(0);
