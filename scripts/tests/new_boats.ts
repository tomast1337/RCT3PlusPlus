// SPDX-License-Identifier: GPL-3.0-or-later
//
// A boat hire built in play: a dock of station pieces laid out on a park's lake, then tested. Its boats appear at
// the dock and sail out over the water, keeping to it; pulled down, the ride takes its boats with it. Give it a
// park with a lake wide open in the middle, such as one with a boat hire of its own:
//
//   ./nob script scripts/tests/new_boats.ts -- --no-intro --no-audio --seed 1 --park PARK

export {};

type Tile = { col: number; row: number };

await until(() => park.loaded(), 1200, "the park");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "tools");
await frames(10);

// Open water for the dock: tiles all on one lake with nothing standing over them, as wide as can be found, with
// water on past the dock's end (it is laid towards higher columns).
const taken = new Set(map.objects().map((o) => `${o.col},${o.row}`));
const { width, height } = map.size();
const water = (c: number, r: number) => map.tile(c, r)?.water;
let middle: Tile | undefined;
const ahead = 2;
for (let reach = 4; reach >= 1 && !middle; --reach)
    for (let r = reach; r < height - reach && !middle; ++r)
        for (let c = reach; c < width - reach - ahead && !middle; ++c) {
            const level = water(c, r);
            let open = level !== undefined;
            for (let dr = -reach; dr <= reach && open; ++dr)
                for (let dc = -reach; dc <= reach + ahead && open; ++dc)
                    open = water(c + dc, r + dr) === level && !taken.has(`${c + dc},${r + dr}`);
            if (open)
                middle = { col: c, row: r };
        }
expect(middle !== undefined, "open water in the park");
log("dock at", JSON.stringify(middle));

const ride = rides.list().length;
expect(build.pick("tracks/TrackedRides/RowingBoats/RowingBoats", "RowingBoats"), "rowing boats to build");
await until(() => build.construction() !== undefined, 600, "the construction panel");
await frames(10);
expect(build.construction()?.ride === ride, "a new ride under construction");
camera.look({ col: middle!.col + 0.5, row: middle!.row + 0.5 }, { col: middle!.col + 0.5, row: middle!.row + 12.5, up: 25 });
await frames(5);
/** Picks the station among the pictured pieces: its button, clicked again to build it. */
async function pickStation(): Promise<string | undefined> {
    for (let i = 0; i < 8; ++i) {
        const special = `placing/body/construction/special${i}`;
        if (ui.find(special) === undefined)
            continue;
        await ui.click(special);
        await frames(10);
        if ((build.construction()?.piece ?? "").toLowerCase().includes("station"))
            return special;
    }
    return undefined;
}
const at = must(map.screen(middle!.col, middle!.row), "the tile in sight");
pointer.move(at.x, at.y);
await frames(10);
const station = await pickStation();
expect(station !== undefined, "a dock piece to start with");
await frames(10);
await pointer.click(at.x, at.y);
await frames(30);
expect((rides.list()[ride].pieces ?? 0) === 1, `the first dock piece laid, not ${rides.list()[ride].pieces}`);
for (let n = 2; n <= 3; ++n) {
    await ui.click(station!);
    await frames(10);
    if ((rides.list()[ride].pieces ?? 0) < n) {
        await ui.click(station!);
        await frames(10);
    }
}
log("dock", JSON.stringify(rides.list()[ride].track), "pieces", rides.list()[ride].pieces);
await ui.click("placing/close");
await frames(10);

// No boats till it is tested; then they wait at the dock and set off over the water.
expect((rides.list()[ride].cars?.length ?? 0) === 0, "no boats before the ride is tested");
const refused = rides.setState(ride, "testing");
expect(refused == null, `the ride tested, not refused with ${refused}`);
await until(() => rides.list()[ride].joined && (rides.list()[ride].cars?.length ?? 0) > 0, 600, "boats at the new dock");
const cars = () => rides.list()[ride].cars!;
const docked = cars();
log("boats", docked.length, "trains", rides.list()[ride].trains, JSON.stringify(docked));

// A path from the west shore out over the lake south of the dock, laid at the shore's height after the boats are out.
// Boats keep off its tiles when it is less than a metre over the water, and may pass under it when higher.
const lane = middle!.row + 3;
let shore = middle!.col;
while (shore > middle!.col - 20 && water(shore, lane) !== undefined)
    --shore;
const laid = map.paths().length;
if (water(shore, lane) === undefined && water(shore - 1, lane) === undefined) {
    await ui.click("tools/Paths/Obj_Paths");
    await until(() => ui.find("footpaths/route") !== undefined, 120, "the path panel");
    await ui.toggle("footpaths/follow", false);
    await ui.toggle("footpaths/route", false);
    camera.look({ col: shore + 0.5, row: lane + 0.5 }, { col: shore + 0.5, row: lane + 10.5, up: 25 });
    await frames(5);
    const from = must(map.screen(shore - 1, lane), `tile ${shore - 1}, ${lane} in sight`);
    const to = must(map.screen(shore, lane), `tile ${shore}, ${lane} in sight`);
    await pointer.drag([from.x, from.y], [to.x, to.y], 10);
    for (let n = 0; n < middle!.col + 3 - shore; ++n)
        await ui.click("footpaths/body/Path_Straight");
    await ui.click("footpaths/close");
    await frames(5);
}
log("bridge", JSON.stringify(map.paths().slice(laid)));
const bridge = map.paths().slice(laid).filter((p) => p.raised && water(p.col, p.row) !== undefined);
expect(bridge.length > 0, "a path laid out over the lake");

await ui.click("bar/Top_FastForward");
await until(() => cars().some((c, b) => Math.hypot(c.col - docked[b].col, c.row - docked[b].row) > 2), 60 * 120, "a boat out on the water");
await ui.click("bar/Top_Play");
// Paths over the water a boat cannot pass under: their decks less than a metre over its surface.
const low_paths = new Set(map.paths().filter((p) => {
    const level = water(p.col, p.row);
    return level !== undefined && p.raised && (p.height ?? 0) < level + 1;
}).map((p) => `${p.col},${p.row}`));
log("paths low over the water", low_paths.size, "bridge clear of the water by", (bridge[0].height ?? 0) - water(bridge[0].col, bridge[0].row)!);
let off_water = 0;
let under_paths = 0;
let seen = 0;
for (let n = 0; n < 60 * 10; ++n) {
    await frames(1);
    for (const c of cars()) {
        ++seen;
        off_water += water(Math.floor(c.col), Math.floor(c.row)) !== undefined ? 0 : 1;
        under_paths += low_paths.has(`${Math.floor(c.col)},${Math.floor(c.row)}`) ? 1 : 0;
    }
}
log("boat frames", seen, "off water", off_water, "under low paths", under_paths);
expect(off_water < seen / 50, `boats keeping to the water, ${off_water} of ${seen} off it`);
expect(under_paths === 0, `boats keeping out from under low paths, ${under_paths} frames under one`);
const out = cars().find((c, b) => Math.hypot(c.col - docked[b].col, c.row - docked[b].row) > 2) ?? cars()[0];
camera.look({ col: out.col, row: out.row }, { col: out.col + 1, row: out.row + 3, up: 12 });
await frames(30);
await screenshot("new_boats_out");

// Pulled down, its boats go with it.
rides.show(ride);
await frames(10);
await ui.click("info/body/Attraction_General_Delete");
await until(() => ui.find("demolish_question") !== undefined, 60, "the question asked");
await ui.click("demolish_question/content/GUI2MiniDialog_Confirm");
await frames(10);
const gone = rides.list()[ride];
expect(gone.demolished && (gone.cars?.length ?? 0) === 0, `the ride and its boats gone (${JSON.stringify({ ...gone, track: undefined })})`);
