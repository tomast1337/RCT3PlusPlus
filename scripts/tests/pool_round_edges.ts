// SPDX-License-Identifier: GPL-3.0-or-later
//
// Swimmers keep to the water's open shape. On the game's blank landscape a pool of six by five squares of water
// goes in beside the road, paved round, laid with loose curves; one corner is laid again with a tight curve and one
// cut across, and a changing room opens between the road and the pool. Many guests come to swim. While they do,
// no swimmer in the water is over the paving or coping round it, and each one getting in does so at the water's
// edge. Close up, a few of those getting in at the curved corners are shot as they dive.
//
//   ./nob script scripts/tests/pool_round_edges.ts -- --no-intro --no-audio --seed 1

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

function on_view(col: number, row: number): [number, number] {
    const at = map.screen(col, row);
    if (!at)
        throw new Error(`tile ${col}, ${row} is not in sight`);
    return [at.x, at.y];
}

const kPool = "Pool/Default/Default_Stub";

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Rides/Obj_Pools") !== undefined || ui.find("tools/Rides") !== undefined, 600, "the tool row");
await until(() => build.flatRides() !== undefined, 1200, "the build items");
const gate = map.paths().reduce((a, b) => (b.row > a.row ? b : a));
// The road's tile beside the room; the pool east of it, the room between.
const c = gate.col;
const r = gate.row - 4;
camera.look({ col: c + 5, row: r + 2 }, { col: c + 5, row: r + 16, up: 18 });
await frames(10);

// Tiles eight by seven east of the room's tile, water six by five inside them, laid with loose curves; then the
// corner far from the road and the room laid with a tight curve, and the one near the road and far from the room
// cut across.
const rides = `tools/Rides/${ui.children("tools/Rides")![0]}`;
if (!ui.find("tools/Rides/Obj_Pools"))
    await ui.click(rides);
await until(() => ui.find("tools/Rides/Obj_Pools") !== undefined, 120, "the rides menu");
await ui.click("tools/Rides/Obj_Pools");
await until(() => ui.find("pool_panel/body/editor/Pool_PlaceTiles") !== undefined, 120, "the pool panel");
await ui.click("pool_panel/body/editor/Pool_PlaceTiles");
await frames(2);
await pointer.drag(on_view(c + 2.5, r - 0.5), on_view(c + 9.5, r + 5.5), 8);
await frames(3);
expect(build.pick(kPool, "Pool_DepthNormal"), "the water in the pool list");
const relay = async (edge: string, a: [number, number], b: [number, number]) => {
    await ui.click(`pool_panel/body/editor/${edge}`);
    await frames(2);
    await pointer.drag(on_view(...a), on_view(...b), 6);
    await frames(3);
};
await relay("Pool_CurveLarge", [c + 3.5, r + 0.5], [c + 8.5, r + 4.5]);
await relay("Pool_CurveSmall", [c + 8.5, r + 4.5], [c + 8.5, r + 4.5]);
await relay("Pool_CurveDiagonal", [c + 3.5, r + 4.5], [c + 3.5, r + 4.5]);
const pieces = () => map.objects().filter((o) => o.overlay === kPool);
const count = (symbol: string) => pieces().filter((o) => o.symbol === symbol).length;
expect(count("Base") === 30, `thirty squares of water, not ${count("Base")}`);
expect(count("CurveCorner9Top") === 2 && count("Corner2Top") === 1, `two sweeps and a corner cut across, not ${count("CurveCorner9Top")} and ${count("Corner2Top")}`);
await ui.click("pool_panel/body/editor/Pool_CurveStraight");
await frames(2);

// The changing room between the road and the pool, opened.
await until(() => ui.find("pool_panel/Pool_ChangingRoomPanel") !== undefined, 120, "the pool panel");
await ui.click("pool_panel/Pool_ChangingRoomPanel");
await frames(2);
await ui.click(`pool_panel/body/${ui.children("pool_panel/body")![0]}`);
await frames(3);
await pointer.click(...on_view(c + 1.5, r + 0.5));
await frames(3);
expect(map.objects().some((o) => o.overlay !== kPool && o.col === c + 1 && o.row === r), "the changing room beside the road");
await until(() => ui.find("info") !== undefined, 120, "the changing room's window");
await ui.click("info/Attraction_General_Closed");
await until(() => ui.find("info/Attraction_Button_Open") !== undefined, 60, "the flag's lights");
await ui.click("info/Attraction_Button_Open");
await frames(3);
expect(ui.find("info/Attraction_General_Open") !== undefined, "the complex open");
await ui.click("info/close");
build.stop();
await frames(2);

// The water's open shape, in tiles. The coping takes a quarter tile in from the walls; the two loose corners keep
// their water within 5/3 tile of a point 1.5 tiles in from their square's middle each way, over the corner square
// and the squares beside it along the walls; the tight one rounds a quarter circle of 0.75 tile, from where the
// coping meets; the cut one drops what lies within 1.25 tile of its outer corner, both ways together. `slack` gives
// the shape that much more room, for how finely the game marks it.
const kCoping = 0.25;
const left = c + 3, right = c + 9, top = r, bottom = r + 5;
const sweeps: [number, number, number, number][] = [[c + 3, r, 1, 1], [c + 8, r, -1, 1]]; // Square, then inward
function dry(col: number, row: number, slack: number): boolean {
    const a = Math.min(col - left, right - col), b = Math.min(row - top, bottom - row);
    if (a < 0 || b < 0)
        return true;
    if (a < kCoping - slack || b < kCoping - slack)
        return true;
    const sc = Math.floor(col), sr = Math.floor(row);
    for (const [kc, kr, ic, ir] of sweeps) {
        const beside = (sc === kc && sr === kr) || (sc === kc + ic && sr === kr) || (sc === kc && sr === kr + ir);
        if (beside && Math.hypot(col - (kc + 0.5 + 1.5 * ic), row - (kr + 0.5 + 1.5 * ir)) > 5 / 3 + slack)
            return true;
    }
    // The tight corner, against the walls of greatest column and row.
    const ta = right - col, tb = bottom - row, centre = kCoping + 0.75;
    if (ta < centre && tb < centre && Math.hypot(centre - ta, centre - tb) > 0.75 + slack)
        return true;
    // The cut corner, against the walls of least column and greatest row.
    if (col - left + (bottom - row) < 1 + kCoping - slack * Math.SQRT2)
        return true;
    return false;
}
expect(dry(c + 3.5, r + 0.5, 0) && !dry(c + 4.2, r + 1.2, 0) && dry(c + 8.9, r + 4.9, 0) && !dry(c + 6, r + 2.5, 0), "the shape's checks to hold");
const kSlack = 0.08; // Of a tile: the game marks the water in sixteenths of a square
const corners = new Set([`${c + 3},${r}`, `${c + 8},${r}`, `${c + 8},${r + 4}`, `${c + 3},${r + 4}`]);

// A warm, sunny day, and many guests.
park.setWeather("sunny");
park.setTemperature(28);
log("dropped", park.dropParties(80, 7).reduce((n, p) => n + p.guests, 0), "guests");
const swimmers = () => people.list().filter((p) => p.state === "swimming" && p.col !== undefined && p.row !== undefined);
await until(() => swimmers().filter((p) => p.in_water).length >= 8, 8000, "guests in the pool");

// Watched for a while: each frame no one in the water over dry ground, and each getting in at the water's edge.
const wet = new Map<string, boolean>();
const over: string[] = [];
const far: string[] = [];
let entries = 0, at_corners = 0, shots = 0, checked = 0;
for (let frame = 0; frame < 6000; ++frame) {
    await frames(1);
    for (const p of swimmers()) {
        const col = p.col!, row = p.row!;
        if (p.in_water) {
            ++checked;
            if (dry(col, row, kSlack))
                over.push(`${p.name} at ${(col - c).toFixed(2)}, ${(row - r).toFixed(2)}`);
        }
        if (p.in_water && wet.get(p.name) === false) {
            ++entries;
            const edge = [[1, 0], [-1, 0], [0, 1], [0, -1], [1, 1], [1, -1], [-1, 1], [-1, -1]].some(([dc, dr]) => dry(col + 0.2 * dc, row + 0.2 * dr, 0));
            if (!edge)
                far.push(`${p.name} at ${(col - c).toFixed(2)}, ${(row - r).toFixed(2)}`);
            if (corners.has(`${Math.floor(col)},${Math.floor(row)}`)) {
                ++at_corners;
                if (shots < 3) {
                    camera.look({ col, row, up: 0 }, { col: col - 1.2, row: row + 2.2, up: 1.6 });
                    await frames(12);
                    await screenshot(`pool_round_edges_dive_${shots++}`);
                }
            }
        }
        wet.set(p.name, p.in_water === true);
    }
}
log("checked", checked, "entries", entries, "at corners", at_corners, "over dry ground", over.length, JSON.stringify(over.slice(0, 8)), "away from the edge", far.length, JSON.stringify(far.slice(0, 8)));
camera.look({ col: c + 6, row: r + 2.5 }, { col: c + 6, row: r + 9, up: 7 });
await frames(10);
await screenshot("pool_round_edges_pool");
expect(entries > 0 && at_corners > 0, `swimmers getting in, some at the corners, not ${entries} and ${at_corners}`);
expect(over.length === 0, `no swimmer in the water over dry ground, not ${over.length}: ${over.slice(0, 8).join("; ")}`);
expect(far.length === 0, `each getting in at the water's edge, not ${far.length}: ${far.slice(0, 8).join("; ")}`);
