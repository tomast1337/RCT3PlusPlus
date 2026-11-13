// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ways between a pool's levels. On a paved deck beside the road stand three raised squares, one, two and three units
// up. A 1m ramp climbs to the first from each of its four sides, a 2m ramp to the second, and a spiral staircase,
// turned each of the four ways, to the third; each ramp turns unasked to the higher square. No railing is left
// across a ramp's foot or top, nor across a staircase's top, and swimmers walk up them and down again.
//
//   ./nob script scripts/tests/pool_ramps.ts -- --no-intro --no-audio --seed 1

export {};

const kPool = "Pool/Default/Default_Stub";
const kSides = [[0, 1], [1, 0], [0, -1], [-1, 0]];

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Rides/Obj_Pools") !== undefined || ui.find("tools/Rides") !== undefined, 600, "the tool row");
await until(() => build.flatRides() !== undefined, 1200, "the build items");
const gate = map.paths().reduce((a, b) => (b.row > a.row ? b : a));
// The changing room on the road's side of the deck; the deck east of it, thirteen squares by five.
const c = gate.col;
const r = gate.row - 4;
const ground = Math.round(map.tile(c + 2, r)!.corners[0]);
const pieces = () => map.objects().filter((o) => o.overlay === kPool);
const railings = (col: number, row: number, height: number) => pieces().filter((o) => o.symbol === "Railing" && o.col === col && o.row === row && o.height === height).length;
const look = async (col: number, row: number, name: string) => {
    camera.look({ col: col + 0.5, row: row + 0.5, up: 1 }, { col: col + 3.5, row: row + 5, up: 5 });
    await frames(10);
    await screenshot(name);
};
camera.look({ col: c + 8, row: r + 0.5 }, { col: c + 8, row: r + 14, up: 14 });
await frames(10);
// Clicked from nearly overhead, so the raised squares hide none of the deck behind them.
const from_above = async (at: { col: number; row: number }) => {
    camera.look({ col: at.col + 0.5, row: at.row + 0.5 }, { col: at.col + 0.5, row: at.row + 2, up: 14 });
    await frames(3);
};

const rides = `tools/Rides/${must(ui.children("tools/Rides"), "tools/Rides shown")[0]}`;
if (!ui.find("tools/Rides/Obj_Pools"))
    await ui.click(rides);
await until(() => ui.find("tools/Rides/Obj_Pools") !== undefined, 120, "the rides menu");
await ui.click("tools/Rides/Obj_Pools");
await until(() => ui.find("pool_panel/body/editor/Pool_PlaceTiles") !== undefined, 120, "the pool panel");
await ui.click("pool_panel/body/editor/Pool_PlaceTiles");
await frames(2);
await pointer.drag(on_view(c + 2.5, r - 1.5), on_view(c + 14.5, r + 2.5), 8);
await frames(3);
expect(pieces().filter((o) => o.symbol === "SolidTop").length === 65, `65 paved squares, not ${pieces().filter((o) => o.symbol === "SolidTop").length}`);
// Two squares of water at the deck's corner by the room, for the swimmers.
expect(build.pick(kPool, "Pool_DepthNormal"), "the water in the pool list");
await pointer.drag(on_view(c + 2.5, r + 2.5), on_view(c + 3.5, r + 2.5), 6);
await frames(3);

// The raised squares, each laid at its own height.
const raised = [{ col: c + 4, step: 1 }, { col: c + 8, step: 2 }, { col: c + 12, step: 3 }];
await ui.click("pool_panel/body/editor/Pool_PlaceTiles");
await ui.click("pool_panel/body/editor/Pool_Height");
for (const { col, step } of raised)
{
    await ui.click("pool_panel/body/editor/height/field");
    keys.press("Ctrl+A");
    keys.type(`${ground + step}`);
    keys.press("Enter");
    await frames(2);
    await pointer.drag(on_view(col + 0.5, r + 0.5), on_view(col + 0.5, r + 0.5), 2);
    await frames(3);
    expect(pieces().some((o) => o.symbol === "SolidTop" && o.col === col && o.row === r && o.height === ground + step), `a square ${step} up at ${col - c}`);
    expect(railings(col, r, ground + step) === 4, `the raised square railed round, not ${railings(col, r, ground + step)}`);
    expect(railings(col - 1, r, ground) === 1, "the deck railed towards it");
}
await ui.click("pool_panel/body/editor/Pool_Height");
build.stop();
await frames(3);

// The ramps, one against each side of the squares a unit and two up, placed nearer the deck's middle than the raised
// square's side, so turned to it only by where it climbs to.
for (const [symbol, { col, step }] of [["Ramp1", raised[0]], ["Ramp2", raised[1]]] as const)
{
    for (const [dc, dr] of kSides)
    {
        expect(build.pick(kPool, symbol), `${symbol} in the pool list`);
        await frames(2);
        const at = { col: col + dc, row: r + dr };
        await from_above(at);
        await pointer.click(...on_view(at.col + 0.5 + 0.2 * dc, at.row + 0.5 + 0.2 * dr));
        await frames(3);
        expect(pieces().some((o) => o.symbol === symbol && o.col === at.col && o.row === at.row && o.height === ground), `${symbol} on the deck at ${dc}, ${dr} from the square ${step} up`);
        expect(railings(at.col, at.row, ground) === 0, `no railing across the ${symbol}'s foot, not ${railings(at.col, at.row, ground)}`);
    }
    expect(railings(col, r, ground + step) === 0, `no railing across the ${symbol}s' tops, not ${railings(col, r, ground + step)}`);
    build.stop();
    await frames(3);
    await look(col, r, `pool_ramps_${symbol}`);
}

// A staircase on each side of the square three up, turned on a quarter each time.
for (const [dc, dr] of kSides)
{
    expect(build.pick(kPool, "SegmentTypeSpiralStairs"), "the spiral staircase in the pool list");
    await frames(2);
    keys.press(".");
    const at = { col: raised[2].col + dc, row: r + dr };
    await from_above(at);
    await pointer.click(...on_view(at.col + 0.5, at.row + 0.5));
    await frames(3);
    expect(pieces().some((o) => o.symbol === "StairsTop" && o.col === at.col && o.row === at.row), `a staircase at ${dc}, ${dr} from the square three up`);
    // Railed at its foot towards the raised square, and at its top on its three other sides.
    expect(railings(at.col, at.row, ground) === 1 && railings(at.col, at.row, ground + 3) === 3, `the staircase railed at its foot once and at its top three times, not ${railings(at.col, at.row, ground)} and ${railings(at.col, at.row, ground + 3)}`);
}
expect(railings(raised[2].col, r, ground + 3) === 0, `no railing across the staircases' tops, not ${railings(raised[2].col, r, ground + 3)}`);
build.stop();
await frames(3);
await look(raised[2].col, r, "pool_ramps_stairs");
camera.look({ col: c + 8, row: r + 0.5 }, { col: c + 8, row: r + 9, up: 8 });
await frames(10);
await screenshot("pool_ramps_deck");

// The changing room by the road, opened; on a warm day guests swim, and walk the ways up and down.
await until(() => ui.find("tools/Rides/Obj_Pools") !== undefined || ui.find("tools/Rides") !== undefined, 120, "the tool row");
if (!ui.find("tools/Rides/Obj_Pools"))
    await ui.click(rides);
await ui.click("tools/Rides/Obj_Pools");
await until(() => ui.find("pool_panel/Pool_ChangingRoomPanel") !== undefined, 120, "the pool panel");
await ui.click("pool_panel/Pool_ChangingRoomPanel");
await frames(2);
await ui.click(`pool_panel/body/${must(ui.children("pool_panel/body"), "pool_panel/body shown")[0]}`);
await frames(3);
await pointer.click(...on_view(c + 1.5, r + 0.5));
// The pool panel stays out once the room is placed; closed, a click on the room opens its window.
await ui.click("pool_panel/close");
await frames(3);
// Looked at from close by, as it stands at the view's edge here.
camera.look({ col: c + 1.5, row: r + 0.5 }, { col: c + 4, row: r + 3.5, up: 3 });
await frames(10);
// Its walls, over the railing in front of it.
const [hut_x, hut_y] = on_view(c + 1.5, r + 0.5);
await pointer.click(hut_x, hut_y - 0.1);
await until(() => ui.find("info") !== undefined, 120, "the changing room's window");
await ui.click("info/Attraction_General_Closed");
await until(() => ui.find("info/Attraction_Button_Open") !== undefined, 60, "the flag's lights");
await ui.click("info/Attraction_Button_Open");
await frames(3);
expect(ui.find("info/Attraction_General_Open") !== undefined, "the complex open");
await ui.click("info/close");
park.setWeather("sunny");
park.setTemperature(28);
park.dropParties(40, 7);

// A swimmer is up when on a raised square or a staircase above its foot, on the way when between the deck and the
// top on a ramp's square or a staircase, and down when on the deck again after being up.
const ways = new Set([...raised.flatMap(({ col }) => kSides.map(([dc, dr]) => `${col + dc},${r + dr}`))]);
const tops = new Set(raised.map(({ col }) => `${col},${r}`));
const up = new Set<string>();
const climbed = new Set<string>();
const walked = new Set<string>();
let between: { col: number; row: number; up: number } | undefined;
const watch = () => {
    for (const p of people.list())
    {
        if (p.state !== "swimming" || p.col === undefined || p.up === undefined)
            continue;
        const tile = `${Math.floor(p.col)},${Math.floor(p.row!)}`;
        const height = p.up - ground;
        if (tops.has(tile) && height > 0.9)
            up.add(p.name);
        if (ways.has(tile) && height > 0.4 && height < 2.6 && !tops.has(tile))
        {
            climbed.add(p.name);
            between ??= { col: p.col, row: p.row!, up: p.up };
        }
        if (up.has(p.name) && height < 0.4 && !ways.has(tile) && !tops.has(tile))
            walked.add(p.name);
    }
    return [...walked].some((name) => climbed.has(name));
};
await park.advanceUntil(watch, 3600, `a swimmer up a way and down again (up ${[...up]}, on the ways ${[...climbed]}, back down ${[...walked]})`);
log("up", up.size, "on the ways", climbed.size, "back down", walked.size, "seen on the way at", JSON.stringify(between));
camera.look({ col: between!.col, row: between!.row, up: between!.up - ground + 1 }, { col: between!.col + 3, row: between!.row + 4, up: between!.up - ground + 3 });
await frames(10);
await screenshot("pool_ramps_walking");
