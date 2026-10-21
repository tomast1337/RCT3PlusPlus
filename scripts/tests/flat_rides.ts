// SPDX-License-Identifier: GPL-3.0-or-later
//
// Places every flat ride the install offers on the game's blank landscape the way a player would: the ride,
// its entrance and its exit, a queue from the entrance and a path from the exit to a road, all roads joined
// to the park's entrance. Then lets the park run and reports each ride that would not place, never joined the
// paths, or carried nobody, to find the rides that need more building or do not work yet.
//
//   ./nob script scripts/tests/flat_rides.ts -- --no-audio
//
// The rides stand in lanes, each above its own road, tallest lanes first:
//
//   |  +-----+   +---+
//   |  |ride |X  |   |X        X exit, E entrance
//   |  +-----+|  +---+|
//   |  E      |  E    |
//   |  q      |  q    |        q queue
//   +=========================  road
//
// Only a failure to set up (a path that would not lay, a ride the script could not reach) fails the script;
// rides that misbehave are listed in the log and the report.

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

/** Tiles of free ground between the side of one ride's slot and the next's. */
const slotGap = 4;
/** Rows between a ride's last row and its lane's road: entrance, queue, queue. */
const toRoad = 4;
/** Frames the park runs at its fastest before the rides are looked at. */
const runFrames = 3600;

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "the tool row and the build lists");
const { width, height } = map.size();
const lastCol = width - 2;
const entrancePaths = map.paths();
expect(entrancePaths.length > 0, "a path from the park's entrance");
// The entrance path's end farthest into the park.
const gate = entrancePaths.reduce((a, b) => (b.row > a.row ? b : a));
const topRoad = gate.row + 2;

/** Looks down on tile (col, row) from the south, high enough that a lane's slot is in sight. */
async function look(col: number, row: number, up = 45): Promise<void> {
    camera.look({ col: col + 0.5, row: row + 0.5 }, { col: col + 0.5, row: row + 0.5 + up / 3, up });
    await frames(2);
}

/** How high `look` must be to see `cols` columns either side of the middle, and `rows` rows: the view is
 * narrow, about 5 columns either side from 80 up. */
function reach(cols: number, rows: number): number {
    return Math.max(60, 17 * cols, 8 * rows);
}

/** Where the middle of tile (col, row) is on the view; throws when it is out of sight. */
function on_view(col: number, row: number): [number, number] {
    const at = map.screen(col, row);
    if (!at)
        throw new Error(`tile ${col}, ${row} is not in sight`);
    return [at.x, at.y];
}

// The rides, laid out in lanes before anything is built so the roads are known.
interface Slot {
    ride: FlatRideItem;
    col: number; ///< Of the ride's first column
    lane: number;
}
const items = build.flatRides()!.slice().sort((a, b) => b.length - a.length || b.width - a.width);
const slots: Slot[] = [];
const roads: number[] = [];
{
    let col = 2;
    let laneTop = topRoad + 1;
    let laneLength = 0;
    for (const ride of items) {
        if (laneLength === 0 || col + ride.width + slotGap > lastCol) {
            if (laneLength > 0) {
                laneTop += laneLength + toRoad;
                roads.push(laneTop - 1);
            }
            col = 2;
            laneLength = ride.length;
        }
        slots.push({ ride, col, lane: roads.length });
        col += ride.width + slotGap;
    }
    roads.push(laneTop + laneLength + toRoad - 1);
}
log("rides", items.length, "lanes", roads.length, "roads at", JSON.stringify(roads));
expect(roads[roads.length - 1] < height - 1, `the lanes to fit on the map (last road at row ${roads[roads.length - 1]})`);

// Paths and queues.
await ui.click("tools/Paths/Obj_Paths");
await until(() => ui.find("footpaths/route") !== undefined, 120, "the path panel");
await ui.toggle("footpaths/follow", false);

/** The style buttons on the panel's page `tab`. */
async function styles(tab: string): Promise<string[]> {
    await ui.click(`footpaths/${tab}`);
    await frames(2);
    return ui.children("footpaths/body") ?? [];
}
const pathPage = await styles("paths");
const queuePage = await styles("queues");
const queueStyle = queuePage.find((n) => !pathPage.includes(n));
const pathStyle = pathPage.find((n) => !queuePage.includes(n));
expect(queueStyle !== undefined && pathStyle !== undefined, `a path style and a queue style (paths ${pathPage}, queues ${queuePage})`);
let queueNow = true;
/** Picks the queue style, or the path style. */
async function choose(queue: boolean): Promise<void> {
    if (queue === queueNow)
        return;
    await ui.click(`footpaths/${queue ? "queues" : "paths"}`);
    await frames(2);
    await ui.click(`footpaths/body/${queue ? queueStyle : pathStyle}`);
    await frames(2);
    queueNow = queue;
}
await choose(false);

/** The tiles with a path on, as "col,row". */
const laid = () => new Set(map.paths().map((p) => `${p.col},${p.row}`));

/** Lays a path from tile `a` to tile `b`, with the camera on the middle of them. */
async function route(a: [number, number], b: [number, number]): Promise<void> {
    await look((a[0] + b[0]) >> 1, (a[1] + b[1]) >> 1, reach(Math.abs(a[0] - b[0]) / 2 + 1, Math.abs(a[1] - b[1]) / 2 + 1));
    await ui.toggle("footpaths/route", false);
    await pointer.click(...on_view(a[0], a[1]));
    await ui.toggle("footpaths/route", true);
    // The route shows once the pointer rests on the goal, which can take a few frames after the camera moved.
    for (let wait = 3; wait <= 24 && !laid().has(`${b[0]},${b[1]}`); wait *= 2) {
        pointer.move(...on_view(b[0], b[1]));
        await frames(wait);
        await pointer.click(...on_view(b[0], b[1]));
    }
    await ui.toggle("footpaths/route", false);
}

/** A road along `row` from column 1 to the last, in pieces short enough to be in sight. */
async function road(row: number): Promise<void> {
    const piece = 10;
    for (let c = 1; c <= lastCol; c += piece)
        await route([c, row], [Math.min(c + piece - 1, lastCol), row]);
}

// From the entrance path to the top road, the roads, and a road down their west end joining them.
await route([gate.col, gate.row + 1], [gate.col, topRoad - 1]);
for (const row of [topRoad, ...roads])
    await road(row);
for (const [i, row] of roads.entries())
    await route([1, (i === 0 ? topRoad : roads[i - 1]) + 1], [1, row - 1]);
const roadGaps: string[] = [];
const paths = laid();
const has = (col: number, row: number) => paths.has(`${col},${row}`);
for (const row of [topRoad, ...roads])
    for (let c = 1; c <= lastCol; ++c)
        if (!has(c, row))
            roadGaps.push(`${c},${row}`);
for (let r = topRoad; r <= roads[roads.length - 1]; ++r)
    if (!has(1, r))
        roadGaps.push(`1,${r}`);
expect(roadGaps.length === 0, `every road tile laid; missing ${roadGaps.slice(0, 20).join(" ")}`);
await look(width >> 1, (topRoad + roads[roads.length - 1]) >> 1, 300);
await screenshot("roads");

// The rides.
interface Outcome {
    slot: Slot;
    trouble: string[];
}
const outcomes: Outcome[] = [];
for (const slot of slots) {
    const { ride, col } = slot;
    const trouble: string[] = [];
    outcomes.push({ slot, trouble });
    const road = roads[slot.lane];
    const last = road - toRoad; // The ride's last row
    const first = last - ride.length + 1;
    const right = col + ride.width - 1;
    await look(col + (ride.width >> 1), last, reach(ride.width / 2 + 3, ride.length / 2 + 2));

    /** Clicks tile (c, r) for the placing step `step`; false, noting why, when it would not take. */
    async function place(step: string, c: number, r: number): Promise<boolean> {
        pointer.move(...on_view(c, r));
        await frames(3);
        const before = build.placing();
        if (before === undefined || before.step !== step) {
            trouble.push(`${step}: not placing it (${JSON.stringify(before)})`);
            return false;
        }
        if (before.problem !== "none") {
            trouble.push(`${step}: ${before.problem}`);
            return false;
        }
        await pointer.click(...on_view(c, r));
        await frames(2);
        if (build.placing()?.step === step) {
            trouble.push(`${step}: the click did not place it (no model?)`);
            return false;
        }
        return true;
    }

    if (!build.pick(ride.overlay, ride.symbol)) {
        trouble.push("cannot be picked");
        continue;
    }
    // A ride is centred on the tile pointed at; its entrance goes under its first column, its exit by its
    // last row on the east.
    const placed = (await place("ride", col + ((ride.width - 1) >> 1), first + ((ride.length - 1) >> 1))) && (await place("entrance", col, last + 1)) && (await place("exit", right + 1, last));
    if (!placed) {
        build.stop();
        await frames(1);
        continue;
    }
    await choose(true);
    await route([col, last + 2], [col, road - 1]);
    await choose(false);
    await route([right + 2, last], [right + 2, road - 1]);
    if (!map.paths().some((p) => p.col === col && p.row === last + 2 && p.queue))
        trouble.push("no queue laid in front of the entrance");
    if (!laid().has(`${right + 2},${road - 1}`))
        trouble.push("no path laid from the exit");
}
await screenshot("rides");

// Rides are built closed: each is tested a while, then opened, as a player would.
for (const [i] of rides.list().entries())
    rides.setState(i, "testing");
await ui.click("bar/Top_SuperFastForward");
await frames(120);
for (const [i] of rides.list().entries())
    rides.setState(i, "open");

// Run the park.
for (let i = 0; i < runFrames; i += 60) {
    await frames(60);
    if (i % 600 === 0)
        log("running", i, "frames;", rides.list().filter((r) => (r.riders ?? 0) > 0).length, "rides carrying riders");
}
await ui.click("bar/Top_Pause");
await look(gate.col, topRoad + 4, 60);
await screenshot("gate");

const all = rides.list();
for (const o of outcomes) {
    const { ride } = o.slot;
    const info = all.find((r) => r.overlay === ride.overlay && r.symbol.split(":")[0] === ride.symbol);
    if (o.trouble.length === 0 && info) {
        if (!info.entrance || !info.exit)
            o.trouble.push("no entrance or exit recorded");
        if (!info.joined)
            o.trouble.push("never joined the paths (no timing from its model?)");
        else if ((info.customers ?? 0) === 0)
            o.trouble.push(`nobody rode it (queue ${info.queue}, line ${info.queueLine} tiles, ${info.onPaths ? "on" : "OFF"} the paths, ${info.open ? "open" : "CLOSED"}, price ${info.price}, cycle ${info.cycleSeconds?.toFixed(1)} s, ratings ${info.ratings?.map((x) => x.toFixed(2))})`);
    } else if (o.trouble.length === 0) {
        o.trouble.push("placed but not in the park's rides");
    }
}
const bad = outcomes.filter((o) => o.trouble.length > 0);
log("guests carried by ride:", all.map((r) => `${r.symbol}=${r.customers ?? "-"}`).join(" "));
for (const o of bad)
    log("TROUBLE", `${o.slot.ride.overlay}/${o.slot.ride.symbol}`, `${o.slot.ride.width}x${o.slot.ride.length}:`, o.trouble.join("; "));
log(`${outcomes.length - bad.length} of ${outcomes.length} flat rides placed, joined and carried guests`);
await look(width >> 1, (topRoad + roads[roads.length - 1]) >> 1, 300);
await screenshot("park");
