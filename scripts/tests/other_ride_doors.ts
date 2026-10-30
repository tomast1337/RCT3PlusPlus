// SPDX-License-Identifier: GPL-3.0-or-later
//
// Rides of the "other rides" list on the game's blank landscape: a vintage car track laid from a saved design,
// and an observation tower set down whole. Each takes its theme's fence: its entrance and exit stand walled on
// both sides and paved underneath, whether the design brought them or they were placed by hand; the tower's
// base is fenced round, open at its doors, and its floor paved.
//
//   ./nob script scripts/tests/other_ride_doors.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "tools");

/** What is wrong, gathered so that every view is shown before the script fails. */
const trouble: string[] = [];
function check(ok: boolean, what: string): void {
    if (!ok)
        trouble.push(what);
}

/** The panels of `fence` standing on the tile. */
const panels = (fence: string, col: number, row: number) => map.objects().filter((o) => o.symbol.toLowerCase() === fence.toLowerCase() && o.col === col && o.row === row).length;
const paved = (col: number, row: number) => map.pavings().some((t) => t.col === col && t.row === row);

/** Checks both doors of ride `i` are walled and paved, and shows them. */
async function doorsDone(i: number, what: string): Promise<void> {
    await frames(10);
    const ride = rides.list()[i];
    check(ride.fence !== "", `${what}: a fence for ${ride.name}`);
    for (const door of [ride.entrance!, ride.exit!]) {
        check(panels(ride.fence, door.col, door.row) >= 2, `${what}: a door at ${door.col},${door.row} walled on both sides, not ${panels(ride.fence, door.col, door.row)} panels`);
        check(paved(door.col, door.row), `${what}: the door at ${door.col},${door.row} paved`);
    }
}

/** Places ride `i`'s entrance or exit from its window, trying across the middle of the view until one takes. */
async function placeDoor(i: number, step: "entrance" | "exit"): Promise<void> {
    rides.show(i);
    await frames(5);
    await ui.click(`info/body/Attraction_General_${step === "entrance" ? "Entrance" : "Exit"}`);
    await frames(3);
    expect(build.placing()?.step === step, `placing the ${step}, not ${JSON.stringify(build.placing())}`);
    const seen = new Set<string>();
    for (let k = 0; k < 625 && build.placing()?.step === step; ++k) {
        const x = 0.26 + 0.02 * (k % 25);
        const y = 0.2 + 0.02 * Math.floor(k / 25);
        pointer.move(x, y);
        await frames(1);
        const pl = build.placing();
        if (pl?.site) seen.add(`${pl.site.col},${pl.site.row},${pl.site.turns}:${pl.problem}`);
        if (build.placing()?.problem === "none") {
            await pointer.click(x, y);
            await frames(2);
        }
    }
    if (build.placing()?.step === step) {
        await screenshot(`no_${step}`);
        log("tried", JSON.stringify([...seen]), JSON.stringify(rides.list()[i]));
        const s = build.placing()!.site!;
        log("near", JSON.stringify(map.objects().filter((o) => Math.abs(o.col - s.col) <= 2 && Math.abs(o.row - s.row) <= 2)));
    }
    expect(build.placing()?.step !== step, `the ${step} placed, ${JSON.stringify(build.placing())}`);
}

function lookAt(spot: Spot, far = 1): void {
    camera.look({ col: spot.col + 0.5, row: spot.row + 0.5 }, { col: spot.col + 0.5 + 2 * far, row: spot.row + 0.5 + 3 * far, up: 2 * far });
}

// The vintage cars, from the first saved design, doors and all.
expect(build.pick("tracks/TrackedRides/VintageCars/VintageCars", "VintageCars", "designs"), "vintage cars to build");
await until(() => ui.find("design_browser") !== undefined, 600, "the designs window");
await frames(3);
expect((build.construction()?.designs ?? []).length > 0, "a saved vintage cars design");
const list = ui.find("design_browser/content/list")!;
const button = ui.find("design_browser/content/build")!;
const unit = (button.bottom - button.top) / 22;
await pointer.click(list.x, list.top + (18 + 16 * 2) * unit);
await frames(5);
await ui.click("design_browser/content/build");
await frames(10);
pointer.move(0.5, 0.45);
await frames(30);
await pointer.click(0.5, 0.45);
await frames(30);
const cars = build.construction()!.ride;
expect(rides.list()[cars].entrance !== null && rides.list()[cars].exit !== null, "the design laid with its doors");
build.stop();
lookAt(rides.list()[cars].entrance!);
await frames(20);
await screenshot("design_doors");
await doorsDone(cars, "doors from the design");

// Its entrance placed again by hand.
await placeDoor(cars, "entrance");
lookAt(rides.list()[cars].entrance!);
await frames(20);
await screenshot("hand_entrance");
await doorsDone(cars, "an entrance placed by hand");

// The observation tower, on clear ground away from the cars: its one piece, then its doors by hand.
const away = rides.list()[cars].entrance!;
camera.look({ col: away.col + 0.5, row: away.row - 24.5 }, { col: away.col + 10.5, row: away.row - 14.5, up: 12 });
await frames(10);
expect(build.pick("tracks/TrackedRides/Observation/Observation", "Observation"), "an observation tower to build");
await until(() => build.construction() !== undefined, 600, "the construction panel");
pointer.move(0.5, 0.6);
await frames(30);
await pointer.click(0.5, 0.6);
await frames(30);
const tower = rides.list().length - 1;
expect((rides.list()[tower].pieces ?? 0) === 1, `the tower set down, not ${JSON.stringify(rides.list()[tower])}`);
await placeDoor(tower, "entrance");
lookAt(rides.list()[tower].entrance!, 3);
await frames(20);
await screenshot("tower_entrance");
camera.look({ col: rides.list()[tower].entrance!.col + 0.5, row: rides.list()[tower].entrance!.row + 0.5 }, { col: rides.list()[tower].entrance!.col + 8.5, row: rides.list()[tower].entrance!.row + 8.5, up: 10 });
await frames(10);
await placeDoor(tower, "exit");
build.stop();
const t = rides.list()[tower];
lookAt(t.entrance!, 3);
await frames(20);
await screenshot("tower");
await doorsDone(tower, "the tower's doors");
// Its base: panels on more tiles than its doors stand on, and paving beyond the doors.
const fenced = new Set(map.objects().filter((o) => o.symbol.toLowerCase() === t.fence.toLowerCase()).map((o) => `${o.col},${o.row}`));
log("tower fenced tiles", fenced.size, "paved", map.pavings().length);
check(fenced.size > 4, `the tower's base fenced round, not ${fenced.size} fenced tiles`);
const doorTiles = [t.entrance!, t.exit!].map((d) => `${d.col},${d.row}`);
const floor = map.pavings().filter((p) => !doorTiles.includes(`${p.col},${p.row}`) && Math.abs(p.col - t.entrance!.col) <= 4 && Math.abs(p.row - t.entrance!.row) <= 4);
check(floor.length > 0, "the tower's floor paved");
expect(trouble.length === 0, trouble.join("; "));
