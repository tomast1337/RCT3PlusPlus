// SPDX-License-Identifier: GPL-3.0-or-later
//
// Raising the ground under a path with guests on it: the path rises with the ground, the guests on it rise with it
// and reel while it moves under their feet. Then tipping it by its near edge, steep enough that the guests on it
// slide down it, fall over and get up again.
//
//   ./nob script scripts/tests/land_paths.ts -- --no-audio

export {};

await park.open("Campaigns/Base/ChequeredFlag.dat");
await park.ready();
await frames(30);
expect(park.dropParties(30, 5).length > 0, "parties dropped in the park");
await frames(10);

// The ground path tile with the most guests on it, on land the park owns and with no path over it.
const over = new Set(
    map
        .paths()
        .filter((p) => p.raised)
        .map((p) => `${p.col},${p.row}`),
);
const ground = new Set(
    map
        .paths()
        .filter((p) => !p.queue && !p.raised && !over.has(`${p.col},${p.row}`))
        .map((p) => `${p.col},${p.row}`),
);
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
const near = () =>
    people
        .list()
        .filter(
            (p) =>
                !p.staff &&
                p.col !== undefined &&
                Math.abs(p.col - (col + 0.5)) < 1.5 &&
                Math.abs(p.row! - (row + 0.5)) < 1.5 &&
                p.state !== "riding",
        );

await ui.click("tools/Land/Land");
await ui.click("tools/Land/Land_Brush_Height");
await until(() => ui.find("land_panel/Land_Vertex") !== undefined, 60, "the land panel's tile tab");
await frames(10); // Let the panel finish opening
// The panel may open on the tile tab already
if (!ui.find("land_panel/body/Land_Vertex_Neighbour")) await ui.click("land_panel/Land_Vertex");
await until(() => ui.find("land_panel/body/Land_Vertex_Neighbour") !== undefined, 60, "the tile tools");
await ui.click("land_panel/body/Land_Vertex_Neighbour");

// By hand, to look at the guests while the ground still moves.
const at = must(map.screen(col + 0.5, row + 0.5), `tile ${col + 0.5}, ${row + 0.5} in sight`);
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
const standing = near().filter(
    (p) => Math.floor(p.col!) === col && Math.floor(p.row!) === row && (p.state === "walking" || p.state === "waiting"),
);
log("ground", before, "->", after, "reeling", reeling, "on the tile", standing.map((p) => p.up!.toFixed(2)).join(" "));
await screenshot("raised");
expect(after > before + 0.25, "the ground under the path to rise");
expect(
    map.paths().some((p) => p.col === col && p.row === row && !p.raised),
    "the path still on the ground there",
);
expect(reeling > 0, "guests by the tile reeling while the ground moved");
expect(
    standing.every((p) => p.up! >= after - 0.05),
    "the guests on the tile standing on the raised path",
);

// Tipped up by its near edge, as far as the path lets it: the guests on it slide down it, towards higher rows.
await frames(60);
log("guests by the tile before tipping", near().length);
await ui.click("tools/Land/Land_Brush_Height");
await until(() => ui.find("land_panel/Land_Vertex") !== undefined, 60, "the land panel's tile tab");
await frames(10);
if (!ui.find("land_panel/body/Land_Vertex_Freeform")) await ui.click("land_panel/Land_Vertex");
await until(() => ui.find("land_panel/body/Land_Vertex_Freeform") !== undefined, 60, "the tile tools");
await ui.click("land_panel/body/Land_Vertex_Freeform");
const edge = must(map.screen(col + 0.5, row + 0.05), `tile ${col + 0.5}, ${row + 0.05} in sight`);
const index = new Map<number, { row: number; up: number; slide: string }>(); // Of people.list, as each starts sliding
const sliding = () =>
    people
        .list()
        .map((p, i) => [p, i] as const)
        .filter(([p]) => p.state === "sliding");
pointer.move(edge.x, edge.y);
pointer.press();
await frames(2);
const tilt = () => Math.max(...map.tile(col, row)!.corners) - Math.min(...map.tile(col, row)!.corners);
for (let i = 1; i <= 150 && tilt() < 1.4; ++i) {
    // Short of how far a path lets its tile tip
    pointer.move(edge.x, edge.y - 0.004 * i);
    await frames(1);
    for (const [p, k] of sliding()) if (!index.has(k)) index.set(k, { row: p.row!, up: p.up!, slide: p.slide! });
}
pointer.release();
await frames(2);
await ui.click("tools/Land/Land_Brush_Height"); // Put away
const tipped = map.tile(col, row)!.corners;
log(
    "tipped corners",
    tipped.map((c) => c.toFixed(2)).join(" "),
    "sliding",
    [...index.values()].map((v) => v.slide).join(" "),
);
expect(index.size > 0, "guests on the tipped tile sliding");
expect(
    [...index.values()].every((v) => ["frontslide", "backslide", "sitslide"].includes(v.slide)),
    "sliders playing a sliding motion",
);
await frames(6);
await screenshot("sliding");
let fallen = 0;
for (let i = 0; i < 600; ++i) {
    await frames(1);
    const list = people.list();
    fallen = [...index.keys()].filter((k) => list[k].state !== "sliding").length;
    if (fallen === index.size) break;
}
const list = people.list();
for (const [k, v] of index)
    log(
        "slid",
        k,
        "col",
        list[k].col!.toFixed(2),
        "row",
        v.row.toFixed(2),
        "->",
        list[k].row!.toFixed(2),
        "up",
        v.up.toFixed(2),
        "->",
        list[k].up!.toFixed(2),
        list[k].state,
        list[k].gesture ?? "",
    );
await screenshot("slid");
expect(fallen === index.size, "every slider to stop");
expect(
    [...index].every(([k, v]) => list[k].row! > v.row && list[k].up! < v.up),
    "every slider further down the slope and lower than they began",
);
expect(
    [...index.keys()].some((k) => list[k].gesture === "knockdown" || list[k].state === "walking"),
    "sliders fallen over or back on their feet",
);
