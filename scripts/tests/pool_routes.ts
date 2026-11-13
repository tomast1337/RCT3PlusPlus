// SPDX-License-Identifier: GPL-3.0-or-later
//
// Swimmers keep to the paving that joins their way. Over a long warm spell, nobody walking in a pool complex is
// ever on a tile with no square of it, nor above or below the paving under them, nor steps from one square to the
// next where the two do not meet: at different heights with no ramp or stairs between, or across a gap. Given a park
// with a pool, it watches that one; else it builds a deck with a pool, a strip beside it joined only at its far end, a
// square a unit up with a ramp to it, one three up with a spiral staircase, a part three up with no way to it and a
// sunken part, and watches that. Shots of the deck and of swimmers up on the ways.
//
//   ./nob script scripts/tests/pool_routes.ts -- --no-intro --no-audio --seed 1 [--park PARK]

export {};

const kPool = "Pool/Default/Default_Stub";
const kDeck = 0.2; ///< The paving's top over a square's surface, in height units, where swimmers walk
const kSlack = 0.12; ///< Chosen by eye: how far off the paving's height a walker may be
const kTick = 0.2; ///< Park seconds between looks
const kWatch = 1200; ///< Park seconds watched: half a game day
const kSides = [[0, 1], [1, 0], [0, -1], [-1, 0]];

// A park given, or none: then the deck is built on a blank one.
try
{
    await until(() => park.loaded(), 600, "the park");
}
catch
{
}
const built = !park.loaded();
let c = 0;
let r = 0;
let ground = 0;
if (built)
{
    await park.open("Campaigns/Base/BlankLandscape.dat");
    await until(() => ui.find("tools/Rides/Obj_Pools") !== undefined || ui.find("tools/Rides") !== undefined, 600, "the tool row");
    await until(() => build.flatRides() !== undefined, 1200, "the build items");
    const gate = map.paths().reduce((a, b) => (b.row > a.row ? b : a));
    c = gate.col;
    r = gate.row - 4;
    ground = Math.round(map.tile(c + 2, r)!.corners[0]);
    const from_above = async (col: number, row: number) => {
        camera.look({ col: col + 0.5, row: row + 0.5 }, { col: col + 0.5, row: row + 2, up: 16 });
        await frames(3);
    };
    const rides = `tools/Rides/${must(ui.children("tools/Rides"), "tools/Rides shown")[0]}`;
    if (!ui.find("tools/Rides/Obj_Pools"))
        await ui.click(rides);
    await until(() => ui.find("tools/Rides/Obj_Pools") !== undefined, 120, "the rides menu");
    await ui.click("tools/Rides/Obj_Pools");
    await until(() => ui.find("pool_panel/body/editor/Pool_PlaceTiles") !== undefined, 120, "the pool panel");
    // Paving dragged from one tile to another, at the height set.
    const pave = async (c0: number, r0: number, c1: number, r1: number) => {
        camera.look({ col: (c0 + c1) / 2 + 0.5, row: (r0 + r1) / 2 + 0.5 }, { col: (c0 + c1) / 2 + 0.5, row: (r0 + r1) / 2 + 12, up: 14 });
        await frames(3);
        await pointer.drag(on_view(c0 + 0.5, r0 + 0.5), on_view(c1 + 0.5, r1 + 0.5), 6);
        await frames(3);
    };
    const height = async (h: number) => {
        await ui.click("pool_panel/body/editor/height/field");
        keys.press("Ctrl+A");
        keys.type(`${h}`);
        keys.press("Enter");
        await frames(2);
    };
    // The deck by the room, nine by four; beside it, past a row with nothing, a strip joined to it only at its far
    // end.
    await ui.click("pool_panel/body/editor/Pool_PlaceTiles");
    await frames(2);
    await pave(c + 2, r - 2, c + 10, r + 1);
    await pave(c + 2, r + 3, c + 10, r + 3);
    await pave(c + 10, r + 2, c + 10, r + 2);
    // Water in the deck.
    expect(build.pick(kPool, "Pool_DepthNormal"), "the water in the pool list");
    await pave(c + 3, r + 1, c + 4, r + 1);
    // A square a unit up, one three up, a part three up off the deck's end with no way to it, and a sunken part.
    await ui.click("pool_panel/body/editor/Pool_PlaceTiles");
    await ui.click("pool_panel/body/editor/Pool_Height");
    await height(ground + 1);
    await pave(c + 6, r - 2, c + 6, r - 2);
    await height(ground + 3);
    await pave(c + 9, r - 2, c + 9, r - 2);
    await pave(c + 11, r - 2, c + 12, r - 1);
    await height(ground - 2);
    await pave(c + 7, r + 1, c + 8, r + 1);
    await ui.click("pool_panel/body/editor/Pool_Height");
    build.stop();
    await frames(3);
    // A ramp up to the square a unit up, and a staircase up to the one three up.
    expect(build.pick(kPool, "Ramp1"), "the ramp in the pool list");
    await frames(2);
    await from_above(c + 6, r - 1);
    await pointer.click(...on_view(c + 6.5, r - 0.3));
    await frames(3);
    build.stop();
    expect(build.pick(kPool, "SegmentTypeSpiralStairs"), "the spiral staircase in the pool list");
    await frames(2);
    keys.press(".");
    await from_above(c + 9, r - 1);
    await pointer.click(...on_view(c + 9.5, r - 0.5));
    await frames(3);
    build.stop();
    await frames(3);
    const pools = map.pools();
    const at = (col: number, row: number) => pools.squares.filter((s) => s.col === col && s.row === row);
    expect(at(c + 6, r - 2).some((s) => s.surface === ground + 1), "a square a unit up");
    expect(pools.ramps.some((p) => p.col === c + 6 && p.row === r - 1), "a ramp up to it");
    expect(at(c + 9, r - 1).some((s) => s.kind === "stairs"), "a staircase");
    expect(at(c + 7, r + 1).some((s) => s.surface === ground - 2), "a sunken part");
    expect(at(c + 11, r - 2).some((s) => s.surface === ground + 3), "a part three up");
    expect(at(c + 2, r + 2).length === 0 && at(c + 2, r + 3).length === 1, "the strip apart from the deck");
    // The changing room by the road, opened.
    if (!ui.find("tools/Rides/Obj_Pools"))
        await ui.click(rides);
    await ui.click("tools/Rides/Obj_Pools");
    await until(() => ui.find("pool_panel/Pool_ChangingRoomPanel") !== undefined, 120, "the pool panel");
    await ui.click("pool_panel/Pool_ChangingRoomPanel");
    await frames(2);
    await ui.click(`pool_panel/body/${must(ui.children("pool_panel/body"), "pool_panel/body shown")[0]}`);
    await frames(3);
    await from_above(c + 1, r);
    await pointer.click(...on_view(c + 1.5, r + 0.5));
    // The pool panel stays out once the room is placed; closed, a click on the room opens its window.
    await ui.click("pool_panel/close");
    await frames(3);
    await pointer.click(...on_view(c + 1.5, r + 0.5));
    await until(() => ui.find("info") !== undefined, 120, "the changing room's window");
    await ui.click("info/Attraction_General_Closed");
    await until(() => ui.find("info/Attraction_Button_Open") !== undefined, 60, "the flag's lights");
    await ui.click("info/Attraction_Button_Open");
    await frames(3);
    await ui.click("info/close");
    park.dropParties(40, 7);
}
await frames(10);
park.setWeather("sunny");
park.setTemperature(28);
park.setTime(0.5);

// The squares by tile, and where each meets the sides of its tile.
const pools = map.pools();
type Square = (typeof pools.squares)[number];
const squares = new Map<string, Square[]>();
for (const s of pools.squares)
    squares.set(`${s.col},${s.row}`, [...(squares.get(`${s.col},${s.row}`) ?? []), s]);
const ramp_on = (s: Square, side: number) => pools.ramps.find((p) => p.col === s.col && p.row === s.row && p.side === side);
const rise = (s: Square) => (s.kind === "slope" ? 1 : s.kind === "steps" ? 2 : s.kind === "stairs" ? Math.max(s.board, 3) : 0);
// The heights a walker may be at on a square: its surface, all a slope's, steps', staircase's or ramp's climb.
const lowest = (s: Square) => s.surface - (s.kind === "spa" ? 1.2 : 0); // A spa's seat is sunk in its water
const highest = (s: Square) => s.surface + Math.max(rise(s), ...pools.ramps.filter((p) => p.col === s.col && p.row === s.row).map((p) => p.step));
// The heights it meets side `side` at: its surface, a slope's or steps' foot or top, a staircase's foot and top.
const meets = (s: Square, side: number): number[] => {
    if (s.kind === "slope" || s.kind === "steps")
        return side === s.side ? [s.surface] : side === (s.side + 2) % 4 ? [s.surface + rise(s)] : [];
    return s.kind === "stairs" ? [s.surface, s.surface + rise(s)] : [s.surface];
};
const under = (col: number, row: number, up: number) => (squares.get(`${col},${row}`) ?? []).filter((s) => up >= lowest(s) + kDeck - kSlack && up <= highest(s) + kDeck + kSlack);
// Whether a walker at `up` on (col, row) may step to the side `side` at `to_up`.
const joined = (col: number, row: number, up: number, side: number, to_up: number) => {
    const [dc, dr] = kSides[side];
    for (const a of under(col, row, up))
        for (const b of under(col + dc, row + dr, to_up))
        {
            if (meets(a, side).some((h) => meets(b, (side + 2) % 4).includes(h)))
                return true;
            const up_ramp = ramp_on(a, side);
            const down_ramp = ramp_on(b, (side + 2) % 4);
            if ((up_ramp && a.surface + up_ramp.step === b.surface) || (down_ramp && b.surface + down_ramp.step === a.surface))
                return true;
        }
    return false;
};

type Seen = { name: string; col: number; row: number; up: number };
const walkers = () =>
    people.list().map((p) => (p.state === "swimming" && p.swimwear && p.col !== undefined && !p.in_water && p.pool_edge === undefined && p.diving === undefined ? { name: p.name, col: p.col, row: p.row!, up: p.up! } : undefined));
const flags = new Map<string, string[]>();
const flag = (what: string, p: Seen, more = "") => flags.set(what, [...(flags.get(what) ?? []), `${p.name} at ${p.col.toFixed(2)} ${p.row.toFixed(2)} ${p.up.toFixed(2)}${more}`]);
// Going in and out at a changing room's door, and on to a slide's platform from the paving beside it.
const platforms = rides.list().flatMap((r) => r.platformSquares ?? []);
const off_pool_ok = (col: number, row: number) => pools.rooms.some((m) => m.col === col && m.row === row) || platforms.some((p) => p.col === col && p.row === row);
let last = walkers();
let looks = 0;
let up_high = 0;
let up_seen: Seen | undefined; // The first walker seen up off the ground's level, shot there
let up_shot = false;
for (let t = 0; t < kWatch / kTick; ++t)
{
    await park.advance(kTick);
    const now = walkers();
    now.forEach((p, i) => {
        if (p === undefined)
            return;
        ++looks;
        const col = Math.floor(p.col);
        const row = Math.floor(p.row);
        if (off_pool_ok(col, row))
            return;
        if (!squares.has(`${col},${row}`))
            flag("off the pool", p);
        else if (under(col, row, p.up).length === 0)
            flag("off the paving's height", p, ` over ${(squares.get(`${col},${row}`) ?? []).map((s) => `${s.kind}@${s.surface}`).join(" ")}`);
        else if (under(col, row, p.up).some((s) => s.surface > ground + 0.5))
        {
            ++up_high;
            up_seen ??= p;
        }
        const was = last[i];
        if (was === undefined || was.name !== p.name)
            return;
        const from = { col: Math.floor(was.col), row: Math.floor(was.row) };
        if ((from.col === col && from.row === row) || off_pool_ok(from.col, from.row))
            return;
        // A step to the next tile, or across a corner by either tile beside it.
        const side = (dc: number, dr: number) => kSides.findIndex(([x, y]) => x === dc && y === dr);
        const dc = col - from.col;
        const dr = row - from.row;
        const ok = Math.abs(dc) + Math.abs(dr) === 1 ? joined(from.col, from.row, was.up, side(dc, dr), p.up) : Math.abs(dc) === 1 && Math.abs(dr) === 1 ? [[dc, 0], [0, dr]].some(([x, y]) => joined(from.col, from.row, was.up, side(x, y), was.up) || joined(from.col, from.row, was.up, side(x, y), p.up)) : false;
        if (!ok)
            flag("a step the paving does not join", p, ` from ${was.col.toFixed(2)} ${was.row.toFixed(2)} ${was.up.toFixed(2)}`);
    });
    last = now;
    if (up_seen && !up_shot)
    {
        up_shot = true;
        camera.look({ col: up_seen.col, row: up_seen.row, up: up_seen.up + 0.5 - (map.tile(Math.floor(up_seen.col), Math.floor(up_seen.row))?.corners[0] ?? 0) }, { col: up_seen.col + 3, row: up_seen.row + 4, up: up_seen.up + 3 });
        park.setTime(0.5);
        await frames(3);
        await screenshot("pool_routes_up");
    }
}
log("looks at walkers", looks, "up off the ground level", up_high);
for (const [what, seen] of flags)
{
    log(what, seen.length);
    for (const s of seen.slice(0, 10))
        log("  ", s);
}
expect(looks > 1000, `walkers to watch, not ${looks} looks`);
expect(flags.size === 0, `nobody off the paving, off its height or across what it does not join (${[...flags].map(([w, s]) => `${w} ${s.length}`).join(", ")})`);
if (built)
    expect(up_high > 0, "walkers up the ramp and the staircase");

// The whole complex.
const middle = pools.squares.reduce((a, s) => ({ col: a.col + s.col / pools.squares.length, row: a.row + s.row / pools.squares.length }), { col: 0, row: 0 });
camera.look({ col: middle.col + 0.5, row: middle.row + 0.5, up: 1 }, { col: middle.col + 0.5, row: middle.row + 10, up: 12 });
park.setTime(0.5);
await frames(10);
await screenshot("pool_routes_complex");
