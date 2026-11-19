// SPDX-License-Identifier: GPL-3.0-or-later
//
// Wild birds: a flock of ducks is added to a park with a lake and watched as it flies in, comes down on the
// water and paddles; gulls are added and seen in the sky; the Debug tab's button takes every bird out again.
//
//   ./nob script scripts/tests/wild_birds.ts -- --no-audio

export {};

await park.open("Campaigns/Base/GoWithTheFlow.dat");
await park.ready();
await frames(30);

// A tile well inside a lake.
const size = map.size();
let lake: { col: number; row: number } | undefined;
for (let row = 2; row < size.height - 2 && !lake; ++row)
    for (let col = 2; col < size.width - 2 && !lake; ++col) {
        const wet = (c: number, r: number) => map.tile(c, r)?.water;
        if (
            [wet(col, row), wet(col + 1, row), wet(col - 1, row), wet(col, row + 1), wet(col, row - 1)].every(
                (w) => w !== undefined,
            )
        )
            lake = { col: col + 0.5, row: row + 0.5 };
    }
expect(lake !== undefined, "a lake in the park");
const { col, row } = lake!;
const flock = () => park.wildBirds().filter((b) => b.kind === "duck");
expect(debug.use("add_ducks") === 12, "a flock of twelve ducks");
const inside = (b: { col: number; row: number }) =>
    b.col > 8 && b.row > 8 && b.col < size.width - 8 && b.row < size.height - 8;
await park.advanceUntil(() => flock().some(inside), 120, "ducks over the park");
await ui.click("bar/Top_Pause");
await frames(3);
const flying = must(flock().find(inside), "ducks over the park");
await look_down(flying.col, flying.row, 24);
await screenshot("ducks flying in");
await ui.click("bar/Top_Pause");
await park.advanceUntil(() => flock().some((b) => b.resting), 120, "ducks down on the water");
const down = flock().find((b) => b.resting)!;
await look_down(down.col, down.row, 9);
await park.advance(5);
await screenshot("ducks on the lake");
await park.advance(10);
await screenshot("ducks paddling");
expect(
    flock().every((b) => map.tile(Math.floor(b.col), Math.floor(b.row))?.water !== undefined || !b.resting),
    "resting ducks on water",
);

// Gulls crossing, the park held still while the camera goes to one.
expect(debug.use("add_gulls") === 3, "three gulls");
const over = () =>
    park
        .wildBirds()
        .find((b) => b.kind === "gull" && b.col > 8 && b.row > 8 && b.col < size.width - 8 && b.row < size.height - 8);
await park.advanceUntil(() => over() !== undefined, 60, "a gull over the park");
await ui.click("bar/Top_Pause");
await frames(3);
const crossing = must(over(), "a gull over the park");
await look_down(crossing.col, crossing.row, 24);
await screenshot("gulls in the sky");
await ui.click("bar/Top_Pause");

// Gulls at litter: they come down by a piece and stand.
debug.use("drop_litter", 60);
await park.advance(4);
expect(debug.use("add_gulls") === 3, "three more gulls");
await park.advanceUntil(
    () => park.wildBirds().some((b) => b.kind === "gull" && b.resting),
    120,
    "a gull on the ground",
);
const standing = must(
    park.wildBirds().find((b) => b.kind === "gull" && b.resting),
    "a standing gull",
);
await ui.click("bar/Top_Pause");
await frames(3);
await look_down(standing.col, standing.row, 10);
await screenshot("a gull at litter");
await ui.click("bar/Top_Pause");

expect(debug.use("remove_wild_birds") >= 3, "the birds taken out");
expect(park.wildBirds().length === 0, "no birds left");
