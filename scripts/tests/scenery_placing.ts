// SPDX-License-Identifier: GPL-3.0-or-later
//
// Places scenery as a player would, on the game's blank landscape, beside the road in from the entrance:
// - a wall goes on the edge of its tile nearest the pointer, whichever way the player turned it;
// - one pointed at where a wall already stands is refused, and with Shift it climbs onto the wall's top;
// - trees planted in a park with none grow woods beyond the map's edge too.
//
//   ./nob script scripts/tests/scenery_placing.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "the build lists");
const paths = map.paths();
const gate = paths.reduce((a, b) => (b.row > a.row ? b : a));
// Grass beside the road, a few tiles in.
const col = gate.col + 2;
const row = gate.row - 4;
camera.look({ col: col + 0.5, row: row + 0.5 }, { col: col + 0.5, row: row + 9.5, up: 20 });
await frames(3);

type Placing = NonNullable<ReturnType<typeof build.placing>>;
/** What placing shows with the pointer resting on point `c`, `r` of the map, in tiles. */
async function hover(c: number, r: number): Promise<{ x: number; y: number; placing: Placing }> {
    const at = map.screen(c, r);
    expect(at !== undefined, `point ${c},${r} in sight`);
    pointer.move(at!.x, at!.y);
    await frames(3);
    const placing = build.placing();
    expect(placing !== undefined, "something placing");
    return { ...at!, placing: placing! };
}

expect(build.pick("Style/Vanilla/Style", "STD4BrickWall3h"), "a brick wall in the scenery lists");
await frames(2);
// Each edge in turn, numbered as turns go: -X, +Z, +X, -Z.
const edges: [number, number][] = [
    [0.1, 0.5],
    [0.5, 0.9],
    [0.9, 0.5],
    [0.5, 0.1],
];
for (const [turns, [dc, dr]] of edges.entries()) {
    const { placing } = await hover(col + dc, row + dr);
    log("wall at", dc, dr, "turns", placing.site?.turns, placing.problem);
    expect(placing.site?.turns === turns, `the wall on edge ${turns} with the pointer at ${dc},${dr}, not ${placing.site?.turns}`);
    expect(placing.problem === "none", `the wall placeable on edge ${turns}, not ${placing.problem}`);
}
const first = await hover(col + 0.1, row + 0.5);
const ground = first.placing.site!.height;
await pointer.click(first.x, first.y);
const again = await hover(col + 0.1, row + 0.5);
expect(again.placing.problem === "blocked", `a second wall refused where the first stands, not ${again.placing.problem}`);
pointer.shift(true);
await frames(3);
const raised = await hover(col + 0.1, row + 0.5);
log("with Shift", raised.placing.problem, "height", raised.placing.site?.height, "from", ground);
expect(raised.placing.problem === "none", `the wall raised with Shift onto the first, not ${raised.placing.problem}`);
expect(raised.placing.site!.height > ground, "the raised wall above the first");
await pointer.click(raised.x, raised.y);
pointer.shift(false);
build.stop();
await frames(2);
await screenshot("walls");

// The park has no trees, so nothing grows beyond its edge until some are planted.
const size = map.size();
const edge = { col: size.width / 2, row: -4 };
camera.look(edge, { col: size.width / 2, row: 30, up: 40 });
await frames(10);
await screenshot("edge_before");
camera.look({ col: col + 0.5, row: row + 0.5 }, { col: col + 0.5, row: row + 9.5, up: 20 });
await frames(3);
expect(build.pick("Style/Vanilla/Style", "Common_Linden_2"), "a tree in the scenery lists");
await frames(2);
for (let i = 0; i < 3; ++i) {
    const tree = await hover(col + 0.5, row - 2.5 - 2 * i);
    expect(tree.placing.problem === "none", `tree ${i} placeable, not ${tree.placing.problem}`);
    await pointer.click(tree.x, tree.y);
}
build.stop();
camera.look(edge, { col: size.width / 2, row: 30, up: 40 });
await frames(10);
await screenshot("edge_after");
quit(0);
