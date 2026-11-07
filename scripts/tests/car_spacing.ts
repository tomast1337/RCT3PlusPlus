// SPDX-License-Identifier: GPL-3.0-or-later
//
// Lays a saved design of coasters whose trains couple their cars with links, or carry their wheels on separate
// axles, on the game's blank landscape, tests it and looks closely at its train waiting in the station. Each link stands
// between the two cars it couples, and cars of the same design stand equally far apart.
//
//   ./nob script scripts/tests/car_spacing.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

/** Lays the first saved design of the coaster `kind` on a new blank landscape, and gives its place in rides.list(). */
async function lay(kind: string): Promise<number> {
    await park.open("Campaigns/Base/BlankLandscape.dat");
    await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "tools");
    expect(build.pick(`tracks/coasters/${kind}/${kind}`, kind, "designs"), `a ${kind} coaster to build`);
    await until(() => ui.find("design_browser") !== undefined, 600, "the designs window");
    await frames(3);
    expect((build.construction()?.designs ?? []).length > 0, `a saved ${kind} design`);
    // The first saved design is a heading and two rows below the custom track, in the window's units (a button is
    // 22 of them high).
    const list = ui.find("design_browser/content/list")!;
    const button = ui.find("design_browser/content/build")!;
    const unit = (button.bottom - button.top) / 22;
    await pointer.click(list.x, list.top + (18 + 16 * 2) * unit);
    await frames(5);
    await ui.click("design_browser/content/build");
    await frames(10);
    pointer.move(0.5, 0.45);
    await frames(30);
    const ride = build.construction()!.ride;
    // Laid, the design's building closes and its window opens.
    await pointer.click(0.5, 0.45);
    await frames(30);
    expect((rides.list()[ride].pieces ?? 0) > 0 && build.construction() === undefined, `the ${kind} design laid`);
    await frames(5);
    return ride;
}

type Car = { col: number; row: number; up?: number };

function apart(a: Car, b: Car): number {
    return Math.hypot(a.col - b.col, a.row - b.row, ((a.up ?? 0) - (b.up ?? 0)) / 4);
}

const problems: string[] = [];
// Coasters with links between their cars, ones whose wheels ride on separate axles, and one whose front car is a
// fixed shape with named points.
for (const kind of ["Giga", "HyperTwist", "LoopingRC", "Drifting", "Reverser", "Stormrunner"]) {
    const ride = await lay(kind);
    // Tested, its train is drawn waiting in the station.
    expect(rides.setState(ride, "testing") == null, `${kind} tested`);
    await until(() => (rides.list()[ride].cars?.length ?? 0) > 0, 600, `${kind}'s cars drawn`);
    const r = rides.list()[ride];
    const cars: Car[] = (r.cars ?? []).slice(0, r.carsPerTrain);
    log(`${kind}: ${r.trains} trains of ${r.passengerCars} cars (${r.carsPerTrain} with links)`);
    expect(cars.length === r.carsPerTrain, `${kind}'s first train drawn, not ${r.cars?.length} of ${r.trains! * r.carsPerTrain!} cars drawn`);
    const linked = r.carsPerTrain! > r.passengerCars!;
    const step = linked ? 2 : 1;
    const gaps: number[] = [];
    for (let k = 0; k + step < cars.length; k += step) {
        const a = cars[k];
        const b = cars[k + step];
        gaps.push(apart(a, b));
        if (linked) {
            // A link stands between the cars it couples, never past either.
            const link = cars[k + 1];
            if (apart(a, link) >= apart(a, b) || apart(link, b) >= apart(a, b))
                problems.push(`${kind}'s link ${k + 1} between its cars, not ${JSON.stringify([a, link, b])}`);
        }
    }
    log(`${kind}: cars apart (tiles) ${gaps.map((g) => g.toFixed(3)).join(" ")}`);
    // None of these cars is longer than 4.4 m with its link: nose to tail, no two stand more than 1.3 tiles apart,
    // nor on top of each other.
    if (gaps.some((g) => g > 1.3 || g < 0.1))
        problems.push(`${kind}'s cars nose to tail, not ${gaps.map((g) => g.toFixed(2))} tiles apart`);
    const front = cars[0];
    const middle = cars[Math.min(2 * step, cars.length - 1)];
    camera.look({ col: (front.col + middle.col) / 2, row: (front.row + middle.row) / 2, up: front.up ?? 0 }, { col: front.col + 2, row: front.row + 3, up: (front.up ?? 0) + 4 });
    await frames(40);
    await screenshot(`${kind}_train`);
}
expect(problems.length === 0, problems.join("; "));
