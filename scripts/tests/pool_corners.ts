// SPDX-License-Identifier: GPL-3.0-or-later
//
// The walls of a pool square for each way water can lie round it. On flat paving, each of the 47 ways the eight
// squares round a water square can hold water (a diagonal counts only where both sides beside it hold water) is
// laid as a block of three by three, apart from the others, then shapes where walls meet: water touching only
// across corners, a paving square in water, a paving square jutting into it, a plus, a T, an L and two pools
// meeting at a corner. Each row is shot from above and at a slant; the log names each block's place and mask.
//
//   ./nob script scripts/tests/pool_corners.ts -- --no-intro --no-audio --no-simulate --seed 1

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

const kPool = "Pool/Default/Default_Stub";

// The neighbours as the pool walls count them: the four sides by turn, then the corners after each side.
const kAround: [number, number][] = [[0, 1], [1, 0], [0, -1], [-1, 0], [1, 1], [1, -1], [-1, -1], [-1, 1]];
const cases: { label: string; rows: string[] }[] = [];
const seen = new Set<number>();
for (let mask = 0; mask < 256; ++mask)
{
    let m = mask & 0x0f;
    for (let t = 0; t < 4; ++t)
        if ((mask & (16 << t)) && (mask & (1 << t)) && (mask & (1 << ((t + 1) & 3))))
            m |= 16 << t;
    if (seen.has(m))
        continue;
    seen.add(m);
    const rows = [[".", ".", "."], [".", "#", "."], [".", ".", "."]];
    kAround.forEach(([dc, dr], bit) => {
        if (m & (1 << bit))
            rows[1 + dr][1 + dc] = "#";
    });
    cases.push({ label: `mask ${m.toString(2).padStart(8, "0")}`, rows: rows.map((r) => r.join("")) });
}
expect(cases.length === 47, `47 ways round a square, not ${cases.length}`);
const shapes: { label: string; rows: string[] }[] = [
    { label: "across corners", rows: ["#.#.#", ".#.#.", "#.#.#", ".#.#.", "#.#.#"] },
    { label: "paving in water", rows: ["###", "#.#", "###"] },
    { label: "paving jutting in", rows: ["##.##", "#####", "#####"] },
    { label: "plus", rows: ["..#..", "..#..", "#####", "..#..", "..#.."] },
    { label: "T", rows: ["######", "######", "..##..", "..##.."] },
    { label: "L", rows: ["##....", "##....", "######", "######"] },
    { label: "two at a corner", rows: ["##..", "##..", "..##", "..##"] },
];
// Rows of blocks, each block its pattern and one square of paving round it.
const kPerRow = 8;
const sections: { label: string; rows: string[] }[][] = [];
for (let i = 0; i < cases.length; i += kPerRow)
    sections.push(cases.slice(i, i + kPerRow));
sections.push(shapes.slice(0, 4), shapes.slice(4));
const kCell = 7;
const width = kPerRow * kCell + 1;
const height = sections.length * kCell + 1;

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Land/Land") !== undefined, 300, "the tool row");
// No money in this park: laying costs nothing.
const size = map.size();
const flat = (c: number, r: number) => {
    const level = map.tile(c, r)?.corners[0] ?? 0;
    for (let row = r - 1; row <= r + height; ++row)
        for (let col = c - 1; col <= c + width; ++col)
        {
            const tile = map.tile(col, row);
            if (!tile?.owned || tile.water !== undefined || tile.corners.some((h) => h !== level))
                return false;
        }
    return true;
};
let site: { col: number; row: number } | undefined;
for (let r = 12; r < size.height - height - 2 && !site; r += 3)
    for (let c = 12; c < size.width - width - 2 && !site; c += 3)
        if (flat(c, r))
            site = { col: c, row: r };
expect(site !== undefined, "flat ground for the grid");
const c0 = site!.col;
const r0 = site!.row;
log("grid at", c0, r0);

function on_view(col: number, row: number): [number, number] {
    const at = map.screen(col, row);
    if (!at)
        throw new Error(`tile ${col}, ${row} is not in sight`);
    return [at.x, at.y];
}

const rides = `tools/Rides/${ui.children("tools/Rides")![0]}`;
await until(() => build.flatRides() !== undefined, 1200, "the build items");
if (!ui.find("tools/Rides/Obj_Pools"))
    await ui.click(rides);
await until(() => ui.find("tools/Rides/Obj_Pools") !== undefined, 120, "the rides menu");
await ui.click("tools/Rides/Obj_Pools");
await until(() => ui.find("pool_panel/body/editor/Pool_PlaceWater") !== undefined, 120, "the pool panel");

const water = () => map.objects().filter((o) => o.overlay === kPool && o.symbol === "Base" && o.col >= c0 && o.col < c0 + width && o.row >= r0 && o.row < r0 + height).length;

let laid = 0;
for (let s = 0; s < sections.length; ++s)
{
    const top = r0 + s * kCell;
    const mid = top + kCell / 2;
    for (const [i, block] of sections[s].entries())
    {
        log(`row ${s} block ${i}: ${block.label}`);
        // The block's paving, then its water, square by square, with the block in sight.
        const left = c0 + i * kCell;
        camera.look({ col: left + kCell / 2, row: mid }, { col: left + kCell / 2, row: mid + 8, up: 12 });
        await frames(8);
        expect(build.pick(kPool, "Pool_PlaceTiles"), "the tiles in the pool list");
        await pointer.drag(on_view(left + 0.5, top + 0.5), on_view(left + kCell - 0.5, top + kCell - 0.5), 6);
        await frames(2);
        expect(build.pick(kPool, "Pool_DepthNormal"), "the water in the pool list");
        for (const [dr, line] of block.rows.entries())
            for (let dc = 0; dc < line.length; ++dc)
                if (line[dc] === "#")
                {
                    const at = on_view(left + 1 + dc + 0.5, top + 1 + dr + 0.5);
                    await pointer.drag(at, at, 2);
                    await frames(2);
                    ++laid;
                }
    }
    build.stop();
    await frames(3);
    expect(water() === laid, `${laid} water squares by row ${s}, not ${water()}`);
}
log("water squares", laid);
// Each block on its own, from above at a slant, once the prices shown for laying it are gone.
await frames(150);
for (let s = 0; s < sections.length; ++s)
    for (const [i, block] of sections[s].entries())
    {
        const col = c0 + i * kCell + 1 + block.rows[0].length / 2;
        const row = r0 + s * kCell + 1 + block.rows.length / 2;
        camera.look({ col, row }, { col, row: row + 3, up: 6 });
        await frames(6);
        await screenshot(`corners_${s}_${i}_${block.label.replaceAll(" ", "_")}`);
    }
