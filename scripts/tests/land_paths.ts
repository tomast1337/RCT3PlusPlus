// SPDX-License-Identifier: GPL-3.0-or-later
//
// Raising the ground under a path with guests on it: the path rises with the ground, the guests on it rise with it
// and reel while it moves under their feet.
//
//   ./nob script scripts/tests/land_paths.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

await park.open("Campaigns/Base/ChequeredFlag.dat");
await park.ready();
await frames(30);
expect(park.dropParties(30, 5).length > 0, "parties dropped in the park");
await frames(10);

// The ground path tile with the most guests on it, on land the park owns and with no path over it.
const over = new Set(map.paths().filter((p) => p.raised).map((p) => `${p.col},${p.row}`));
const ground = new Set(map.paths().filter((p) => !p.queue && !p.raised && !over.has(`${p.col},${p.row}`)).map((p) => `${p.col},${p.row}`));
const counts = new Map<string, number>();
for (const p of people.list())
    if (!p.staff && p.col !== undefined && p.state !== "riding") {
        const key = `${Math.floor(p.col)},${Math.floor(p.row!)}`;
        if (ground.has(key) && map.tile(Math.floor(p.col), Math.floor(p.row!))?.owned)
            counts.set(key, (counts.get(key) ?? 0) + 1);
    }
const busiest = [...counts].sort((a, b) => b[1] - a[1])[0];
expect(busiest !== undefined, "guests on a ground path");
const [col, row] = busiest[0].split(",").map(Number);
log("raising tile", col, row, "with", busiest[1], "guests");
camera.look({ col: col + 0.5, row: row + 0.5 }, { col: col - 4, row: row + 4, up: 14 });
await frames(5);

const low = () => Math.min(...map.tile(col, row)!.corners);
const before = low();
const near = () => people.list().filter((p) => !p.staff && p.col !== undefined && Math.abs(p.col - (col + 0.5)) < 1.5 && Math.abs(p.row! - (row + 0.5)) < 1.5 && p.state !== "riding");

await ui.click("tools/Land/Land");
await ui.click("tools/Land/Land_Brush_Height");
await until(() => ui.find("land_panel/Land_Vertex") !== undefined, 60, "the land panel's tile tab");
await frames(10); // Let the panel finish opening
if (!ui.find("land_panel/body/Land_Vertex_Neighbour"))
    await ui.click("land_panel/Land_Vertex"); // The panel may open on the tile tab already
await until(() => ui.find("land_panel/body/Land_Vertex_Neighbour") !== undefined, 60, "the tile tools");
await ui.click("land_panel/body/Land_Vertex_Neighbour");

// By hand, to look at the guests while the ground still moves.
const at = map.screen(col + 0.5, row + 0.5)!;
pointer.move(at.x, at.y);
pointer.press();
await frames(2);
let reeling = 0;
for (let i = 1; i <= 30; ++i) {
    pointer.move(at.x, at.y - 0.001 * i);
    await frames(1);
    reeling = Math.max(reeling, near().filter((p) => p.gesture === "dizzy").length);
}
await screenshot("reeling");
pointer.release();
await frames(2);
await ui.click("tools/Land/Land_Brush_Height"); // Put away
await frames(2);

const after = low();
const standing = near().filter((p) => Math.floor(p.col!) === col && Math.floor(p.row!) === row && (p.state === "walking" || p.state === "waiting"));
log("ground", before, "->", after, "reeling", reeling, "on the tile", standing.map((p) => p.up!.toFixed(2)).join(" "));
await screenshot("raised");
expect(after > before + 0.25, "the ground under the path to rise");
expect(map.paths().some((p) => p.col === col && p.row === row && !p.raised), "the path still on the ground there");
expect(reeling > 0, "guests by the tile reeling while the ground moved");
expect(standing.every((p) => p.up! >= after - 0.05), "the guests on the tile standing on the raised path");
