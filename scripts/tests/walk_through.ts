// SPDX-License-Identifier: GPL-3.0-or-later
//
// A walk-through, the aquarium, on the game's blank landscape: its saved design laid south of the park's gate with
// its doors, paths from the gate's road to its entrance and from its exit, and opened. Guests dropped in the park
// queue at the entrance, go in one by one, walk its track on foot to the exit and come out onto the path. It has no
// cars, so its test is refused (offered, greyed) and it is rated without a lap once open.
//
//   ./nob script scripts/tests/walk_through.ts -- --no-audio

export {};

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "tools");
await until(() => build.designs() !== undefined, 1200, "the saved designs");
const design = must(
    build.designs()!.find((d) => d.symbol === "Aquarium" && d.overlay !== null),
    "a saved aquarium design",
);
const gate = map.paths().reduce((a, b) => (b.row > a.row ? b : a));

// Picked from its designs window (its first design, under the custom track's row) and laid south of the gate.
expect(build.pick(design.overlay!, design.symbol, "designs"), "the aquarium's designs");
await until(() => ui.find("design_browser") !== undefined, 600, "the designs window");
await frames(3);
const list = must(ui.find("design_browser/content/list"), "design_browser/content/list shown");
const button = must(ui.find("design_browser/content/build"), "design_browser/content/build shown");
const unit = (button.bottom - button.top) / 22; // A button is 22 of the window's units high
await pointer.click(list.x, list.top + (18 + 16 + 8) * unit);
await frames(5);
await ui.click("design_browser/content/build");
await frames(10);
expect(ui.find("design_browser") === undefined, "the aquarium design built from its window");
const ride = build.construction()!.ride;
const goal = { col: gate.col + 2, row: gate.row + 10 };
let at = { ...goal };
let aimed: [number, number] = [0, 0];
for (let i = 0; i < 4; ++i) {
    await look_down(at.col, at.row, 60);
    aimed = on_view(at.col, at.row);
    pointer.move(...aimed);
    await frames(4);
    const g = must(build.construction()?.ghostAt, "the design's ghost");
    if (g.col === goal.col && g.row === goal.row) break;
    at = { col: at.col + goal.col - g.col, row: at.row + goal.row - g.row };
}
if (build.construction()!.refusal === "PlaceObject_IntersectsGround") {
    await ui.click("design_placing/TerrainFlattener_SmoothOff");
    await frames(3);
}
expect(build.construction()!.refusal === "", `the design may stand, not ${build.construction()!.refusal}`);
await pointer.click(...aimed);
await frames(30);
if (build.construction() !== undefined) build.stop();
await frames(5);
const info = () => tracked(rides.list()[ride], "the aquarium");
expect(info().pieces > 0, "the aquarium laid");
expect(info().tested && !info().measured, "a kind that offers a test but is rated without a lap");
log("track", JSON.stringify(info().track), "stations", JSON.stringify(info().stations));

const pieces = map.objects().filter((o) => o.overlay.toLowerCase().startsWith("tracks/"));
const middle = {
    col: pieces.reduce((s, o) => s + o.col, 0) / pieces.length,
    row: pieces.reduce((s, o) => s + o.row, 0) / pieces.length,
};
// The square a door opens onto, by the way it faces.
const kBehind: [number, number][] = [
    [1, 0],
    [0, -1],
    [-1, 0],
    [0, 1],
];
const outside = (door: Spot): [number, number] => [
    door.col + kBehind[door.direction & 3][0],
    door.row + kBehind[door.direction & 3][1],
];
const entrance = must(info().stations.find((s) => s.entrance)?.entrance, "the aquarium's entrance");
const exit = must(info().stations.find((s) => s.exit)?.exit, "the aquarium's exit");
log("entrance", JSON.stringify(entrance), "exit", JSON.stringify(exit));

await ui.click("tools/Paths/Obj_Paths");
await until(() => ui.find("footpaths/route") !== undefined, 120, "the path panel");
await ui.toggle("footpaths/follow", false);
// Begun on the door itself, a trail starts at its height: the design may stand its doors off the ground.
async function route(door: Spot, b: [number, number]): Promise<void> {
    await look_down(
        (door.col + b[0]) >> 1,
        (door.row + b[1]) >> 1,
        Math.max(60, 9 * (Math.abs(door.col - b[0]) + Math.abs(door.row - b[1]))),
    );
    await ui.toggle("footpaths/route", false);
    const floor =
        map.objects().find((o) => o.col === door.col && o.row === door.row && /(Entrance|Exit)$/.test(o.symbol))
            ?.height ?? 0;
    const on_door = must(map.screen(door.col + 0.5, door.row + 0.5, floor + 1), "the door in sight");
    pointer.move(on_door.x, on_door.y);
    await frames(4);
    await pointer.click(on_door.x, on_door.y);
    // A route lays at least a piece a square, less the gate's own path it meets.
    const routed = map.paths().length + Math.max(Math.abs(door.col - b[0]) + Math.abs(door.row - b[1]) - 4, 2);
    await ui.toggle("footpaths/route", true);
    for (let wait = 1; wait <= 96 && map.paths().length < routed; wait *= 2) {
        pointer.move(...on_view(...b));
        await frames(wait);
        await pointer.click(...on_view(...b));
    }
    await ui.toggle("footpaths/route", false);
    expect(
        has_path(...outside(door)) && map.paths().length >= routed,
        `a path from the door at ${door.col}, ${door.row} to ${b}`,
    );
}
await route(entrance, [gate.col, gate.row + 1]);
await route(exit, outside(entrance));
await ui.click("footpaths/close");
await frames(5);

// Its test refused, it opens straight away and guests come.
const test = rides.setState(ride, "testing");
expect(test === "CannotTestTrackedRide", `the aquarium's test refused, not ${test}`);
expect(rides.setState(ride, "open") == null, "the aquarium opened");
await park.advanceUntil(() => info().joined === true, 10, "the aquarium run by the park");
expect(info().graphSamples === 0, "no lap measured");
await park.advanceUntil(() => info().onPaths === true, 10, "the aquarium's entrance on the paths");
park.dropParties(80, 4);
await park.advanceUntil(() => (info().riders ?? 0) >= 3, 600, "guests inside the aquarium");
const inside = info().riders ?? 0;
log("inside", inside, "queue", info().queue, "capacity", info().capacity, "ratings", JSON.stringify(info().ratings));
expect((info().ratings ?? [0])[0] > 0, "rated once open");
// On a while, so they spread along its track.
await park.advance(15);
const walking = people.list().filter((p) => !p.staff && "state" in p && p.state === "riding" && p.at !== null);
log("walking", JSON.stringify(walking.slice(0, 5).map((p) => p.at)));
// The one farthest in, to see them walking its track.
const seen =
    walking
        .map((p) => p.at!)
        .reduce<{ col: number; row: number } | undefined>(
            (a, b) =>
                a &&
                Math.hypot(a.col - entrance.col, a.row - entrance.row) >
                    Math.hypot(b.col - entrance.col, b.row - entrance.row)
                    ? a
                    : b,
            undefined,
        ) ?? middle;
camera.look({ col: seen.col, row: seen.row, up: 5 }, { col: seen.col + 4, row: seen.row + 4, up: 12 });
await frames(20);
await screenshot("walk_through_inside");
await look_down(middle.col, middle.row, 50);
await frames(10);
await screenshot("walk_through_above");

// Out again: three have walked the whole way round and left.
await park.advanceUntil(() => (info().customers ?? 0) - (info().riders ?? 0) >= 3, 600, "guests out at the exit");
log("customers", info().customers, "inside", info().riders);
await look_down(outside(exit)[0], outside(exit)[1], 25);
await frames(10);
await screenshot("walk_through_out");
