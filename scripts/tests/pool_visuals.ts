// SPDX-License-Identifier: GPL-3.0-or-later
//
// How pool slides look where they meet a pool complex. A pool ride's window has no fence, entrance or exit
// buttons but one that opens the pool panel on its paths page, to join the ride to a complex. Where the complex's
// paving meets a side of a slide's station, its platform shows steps down that side; taken up, the steps go.
// Give it a park the game saved with a pool slide whose station a pool's paving meets, beside a pool it ends in:
//
//   ./nob script scripts/tests/pool_visuals.ts -- --no-intro --no-audio --seed 1 --park PARK

export {};

const kPool = "Pool/Default/Default_Stub";

await until(() => park.loaded(), 1200, "the park");
await frames(10);
park.setWeather("sunny");
park.setTime(0.5);

const platforms = () => map.objects().filter((o) => o.overlay.startsWith("Tracks/Platforms"));
const stepped = platforms().filter((o) => /stair/i.test(o.symbol));
log("platforms", platforms().length, "with steps", stepped.length);
expect(stepped.length > 0, "a slide platform with steps where a pool's paving meets it");

// A pool ride's window: the pool path button in place of the doors and fence.
let shown = false;
for (const [i, r] of rides.list().entries()) {
    if (r.demolished) continue;
    rides.show(i);
    await frames(10);
    if (ui.find("info/body/Pool_PathPanel") === undefined) continue;
    log("pool ride", r.name);
    expect(
        ui.find("info/body/Attraction_General_Entrance") === undefined &&
            ui.find("info/body/Attraction_General_PickFence") === undefined,
        "no entrance or fence button for a pool ride",
    );
    shown = true;
    break;
}
expect(shown, "the pool path button in a pool ride's window");
const step = stepped[0];
const up = step.height ?? 0;
camera.look({ col: step.col + 0.5, row: step.row + 0.5, up: up }, { col: step.col + 6, row: step.row + 5, up: up + 5 });
await frames(20);
await screenshot("pool_visuals_window");
await ui.click("info/body/Pool_PathPanel");
await until(() => ui.find("pool_panel/Pool_PathPanel")?.on === true, 120, "the pool panel on its paths page");
await frames(5);
await screenshot("pool_visuals_paths_page");

// The paving across from a stepped platform taken up: its steps go that side.
const before = platforms().map((o) => `${o.col},${o.row}:${o.symbol}`);
const kSteps: [number, number][] = [
    [0, 1],
    [1, 0],
    [0, -1],
    [-1, 0],
];
const { squares } = map.pools();
const paved = kSteps
    .map(([dc, dr]) => squares.find((s) => s.col === step.col + dc && s.row === step.row + dr && s.kind !== "water"))
    .find((s) => s !== undefined);
expect(paved !== undefined, "a paved square beside the stepped platform");
expect(build.pick(kPool, "Pool_DeleteArea"), "the pool delete tool");
camera.look(
    { col: paved!.col + 0.5, row: paved!.row + 0.5, up: up },
    { col: paved!.col + 0.5, row: paved!.row + 20, up: up + 30 },
);
await frames(5);
const p = must(map.screen(paved!.col, paved!.row), "the tile in sight");
await pointer.drag([p.x, p.y], [p.x, p.y], 4);
await frames(10);
build.stop();
await frames(10);
const after = platforms().find((o) => o.col === step.col && o.row === step.row);
log("platform", step.symbol, "now", after?.symbol);
expect(after !== undefined && after.symbol !== step.symbol, "the platform's steps gone with the paving");
const changed = platforms()
    .map((o) => `${o.col},${o.row}:${o.symbol}`)
    .filter((o) => !before.includes(o));
log("changed", changed.join(" "));
expect(changed.length === 1, "the other platforms' steps as the game saved them");
camera.look({ col: step.col + 0.5, row: step.row + 0.5, up: up }, { col: step.col + 6, row: step.row + 5, up: up + 5 });
await frames(20);
await screenshot("pool_visuals_steps_gone");

// Each slide's water fades where it stops: out along a piece before a dry lift, and over the last stretch of an open
// end. An open end over a pool keeps rings spreading on the water where its water falls in.
const ends = rides.list().filter((r) => !r.demolished && r.name !== undefined);
log("rides", ends.map((r) => r.name).join(", "));
const slide_ends = map.objects().filter((o) => /^EndStraight|^SlideEnd/i.test(o.symbol));
for (const [k, e] of slide_ends.entries()) {
    const h = e.height ?? 0;
    camera.look({ col: e.col + 0.5, row: e.row + 0.5, up: h }, { col: e.col + 2.5, row: e.row + 2.5, up: h + 7 });
    await frames(90);
    await screenshot(`pool_visuals_end_${k}`);
    camera.look({ col: e.col + 0.5, row: e.row + 0.5, up: h }, { col: e.col - 3, row: e.row + 2, up: h + 5 });
    await frames(30);
    await screenshot(`pool_visuals_end_${k}_side`);
    const rings = map.rings().filter((r) => Math.abs(r.col - e.col) < 4 && Math.abs(r.row - e.row) < 4);
    log("end", e.symbol, e.col, e.row, "rings near", rings.length);
}
log("POOL_VISUALS ok");
