// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ride cars' moving parts, each on a saved design laid on the blank landscape and tested, watched every twentieth of
// a second:
//
//   - Green Meanies: its tubs stay upright while their bases climb straight up and loop;
//   - Flight of the Phoenix: its cars swing out in the curves and hang straight again at the station;
//   - Dizzymouse: its tubs spin in the turns and square up before the station;
//   - Splash Down and Water Python: dinghies and bobsleighs sit upright at the station and swing in the turns;
//   - Toxic Toboggan: the ends of the links between its sleds meet the sleds where they couple on.
//
// A screenshot of each at the moment its part shows most, of the dinghies and bobsleighs at the station too, and of
// Toxic Toboggan's links.
//
//   ./nob script scripts/tests/car_parts.ts -- --no-audio

export {};

/** Lays the saved design whose file holds `name` (its kind in brackets first, where another kind has a design of
 * that name) on a new blank landscape, and gives its place in rides.list(). */
async function lay(name: string): Promise<RideIndex> {
    await park.open("Campaigns/Base/BlankLandscape.dat");
    await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "tools");
    await until(() => build.designs() !== undefined, 1200, "the saved designs");
    const d = must(
        build.designs()!.find((d) => d.file.includes(name)),
        `the ${name} design`,
    );
    expect(build.pick(must(d.overlay, `${name}'s ride`), d.symbol, "designs"), `${name}'s kind to build`);
    await until(() => ui.find("design_browser") !== undefined, 600, "the designs window");
    await frames(3);
    const row = build.construction()!.designs.findIndex((f) => f.includes(name.replace(/^\[.*\]/, "")));
    expect(row >= 0, `${name} among ${build.construction()!.designs.join(", ")}`);
    const list = must(ui.find("design_browser/content/list"), "design_browser/content/list shown");
    const button = must(ui.find("design_browser/content/build"), "design_browser/content/build shown");
    const unit = (button.bottom - button.top) / 22; // A button is 22 of the window's units high
    // Under the column heads and the custom track's row, which stays first.
    await pointer.click(list.x, list.top + (18 + 16 * (row + 1) + 8) * unit);
    await frames(5);
    await ui.click("design_browser/content/build");
    await frames(10);
    // Somewhere it may stand, looking down on the middle of the map.
    const { width, height } = map.size();
    const middle = { col: Math.floor(width / 2), row: Math.floor(height / 2) };
    camera.look(middle, { col: middle.col, row: middle.row + 60, up: 90 });
    await frames(5);
    let here: [number, number] = [0.5, 0.5];
    let spot = false;
    for (let k = 0; k < 60 && !spot; ++k) {
        here = [0.2 + 0.05 * (k % 12), 0.3 + 0.1 * Math.floor(k / 12)];
        pointer.move(...here);
        await frames(3);
        const c = build.construction();
        spot = c !== undefined && c.ghost && c.refusal === "";
    }
    expect(spot, `clear ground for ${name}: ${build.construction()?.refusal}`);
    await pointer.click(...here);
    await frames(30);
    const ride = must(rides.list().at(-1), "the ride laid").index;
    expect(tracked(rides.list()[ride], name).pieces > 0, `${name} laid`);
    // The construction panel shut, as when the player tests from the ride's window: taking out the ground tool and
    // putting it away shuts it.
    await ui.click("tools/Land/Land");
    await frames(3);
    await ui.click("tools/Land/Land_Brush_Height");
    await until(() => ui.find("land_panel/Land_Vertex") !== undefined, 60, "the land panel");
    await frames(5);
    await ui.click("tools/Land/Land_Brush_Height");
    await frames(5);
    return ride;
}

type Car = MapPoint & { pitch: number; partTilt?: number };
type Train = NonNullable<RideRun["trainsAt"]>[number];

/** Tests `ride` for `seconds` park seconds, calling `watch` with its cars and trains every twentieth of one. */
async function watchTest(
    ride: RideIndex,
    seconds: number,
    watch: (cars: Car[], trains: Train[]) => Promise<void>,
): Promise<void> {
    expect(rides.setState(ride, "testing") == null, "tested");
    for (let k = 0; k < seconds * 20; ++k) {
        await park.advance(1 / 20);
        const r = tracked(rides.list()[ride], "the ride");
        await watch(r.cars, r.trainsAt ?? []);
    }
}

/** Looks at `car` from close by its side, `rise` metres above it (higher to see into a trough), paused so it stays
 * there, and saves a screenshot named `name`. */
async function shoot(car: MapPoint, name: string, rise = 2): Promise<void> {
    await ui.click("bar/Top_Pause");
    // The camera looks at a point on the ground: one past the car on the line from the eye through it.
    const up = car.up ?? 0;
    const eye = { col: car.col + 2, row: car.row + 3, up: up + rise };
    const past = up / rise;
    camera.look({ col: car.col - 2 * past, row: car.row - 3 * past, up: 0 }, eye);
    await frames(5);
    await screenshot(name);
    await ui.click("bar/Top_Pause");
}

// Held level: however far a base leans, its tub stays upright.
{
    const ride = await lay("Green Meanies");
    let steepest = 0; // The steepest a base climbed or fell
    let worst = 0; // The furthest a tub leant
    let shot = false;
    await watchTest(ride, 90, async (cars) => {
        for (const c of cars) {
            if (c.partTilt === undefined) continue;
            steepest = Math.max(steepest, Math.abs(c.pitch));
            worst = Math.max(worst, c.partTilt);
            if (!shot && Math.abs(c.pitch) > 1.4 && (c.up ?? 0) > 10) {
                shot = true;
                await shoot(c, "car_parts_level_on_the_lift");
            }
        }
    });
    log("Green Meanies: bases climbed or fell up to", steepest.toFixed(3), "rad, tubs leant up to", worst.toFixed(3));
    expect(steepest > 1.4, `a Green Meanies base on its vertical lift, not only ${steepest.toFixed(3)} rad steep`);
    expect(worst < 0.05, `Green Meanies tubs upright, not leaning ${worst.toFixed(3)} rad`);
}

// Swinging: out in the curves, straight again at rest.
{
    const ride = await lay("Flight of the Phoenix");
    let widest = 0;
    let shot = false;
    await watchTest(ride, 120, async (cars, trains) => {
        for (const [t, train] of trains.entries())
            for (const [k, swing] of train.swings.entries()) {
                widest = Math.max(widest, Math.abs(swing));
                const car = cars[t * train.swings.length + k];
                if (!shot && train.speed > 5 && Math.abs(swing) > 0.5 && car !== undefined) {
                    shot = true;
                    await shoot(car, "car_parts_swung_out");
                }
            }
    });
    log("Flight of the Phoenix: swung up to", widest.toFixed(3), "rad");
    expect(widest > 0.3, `Flight of the Phoenix cars swung out, only ${widest.toFixed(3)} rad`);
    // Closed, its trains come in and stand; their cars hang straight.
    rides.setState(ride, "closed");
    await park.advanceUntil(
        () =>
            (tracked(rides.list()[ride], "the ride").trainsAt ?? []).every((t) => t.station !== null && t.speed === 0),
        300,
        "the trains in",
    );
    await park.advance(20);
    const hanging = (tracked(rides.list()[ride], "the ride").trainsAt ?? []).flatMap((t) => t.swings);
    log("Flight of the Phoenix standing:", hanging.map((s) => s.toFixed(3)).join(" "));
    expect(
        hanging.every((s) => Math.abs(s) < 0.02),
        `standing cars hang straight, not ${hanging.map((s) => s.toFixed(3))}`,
    );
}

// Spinning: tubs turn on their bases in the turns, and square up before the station.
{
    const ride = await lay("Dizzymouse");
    let furthest = 0;
    let shot = false;
    await watchTest(ride, 150, async (cars, trains) => {
        for (const [t, train] of trains.entries()) {
            const car = cars[t * Math.max(train.spins.length, 1)];
            for (const spin of train.spins) {
                if (train.station === null) furthest = Math.max(furthest, Math.abs(spin));
                if (!shot && train.speed > 2 && Math.abs(spin) > 1.2 && car !== undefined) {
                    shot = true;
                    await shoot(car, "car_parts_spun_round");
                }
            }
        }
    });
    log("Dizzymouse: spun up to", furthest.toFixed(3), "rad");
    expect(furthest > 0.5, `Dizzymouse tubs spun, only ${furthest.toFixed(3)} rad`);
    // Closed, its trains come in and stand, and their tubs square up to their one way to stop: facing ahead.
    rides.setState(ride, "closed");
    await park.advanceUntil(
        () =>
            (tracked(rides.list()[ride], "the ride").trainsAt ?? []).every((t) => t.station !== null && t.speed === 0),
        300,
        "the trains in",
    );
    await park.advance(20);
    const standing = (tracked(rides.list()[ride], "the ride").trainsAt ?? []).flatMap((t) => t.spins);
    log("Dizzymouse standing:", standing.map((s) => s.toFixed(3)).join(" "));
    const off = Math.max(...standing.map(Math.abs));
    expect(off < 0.02, `Dizzymouse tubs squared up at the station, not ${off.toFixed(3)} rad off`);
    const car = tracked(rides.list()[ride], "the ride").cars[0];
    await shoot(car, "car_parts_squared_up");
}

// Swinging in the car's own frame, whichever way its pivot bones are turned: upright at the station, swung in the turns,
// at least `least` radians. A dinghy rides up its trough's walls whole, so its own swing stays small.
for (const [design, name, least] of [
    ["[Dingy]Splash Down", "dinghies", 0.06],
    ["Water Python", "bobsleighs", 0.2],
] as const) {
    const ride = await lay(design);
    expect(rides.setState(ride, "testing") == null, "tested");
    await park.advance(0.5);
    // Those with a part, on level track: a part swinging about its car's forward leans back with its car on a slope.
    const waiting = tracked(rides.list()[ride], "the ride").cars.filter(
        (c) => c.partTilt !== undefined && Math.abs(c.pitch) < 0.05,
    );
    expect(waiting.length > 0, `${design}'s ${name} on level track`);
    const leant = Math.max(...waiting.map((c) => c.partTilt!));
    log(`${design} on level track: parts leant up to`, leant.toFixed(3), "rad");
    expect(leant < 0.05, `${design}'s ${name} upright on level track, not leaning ${leant.toFixed(3)} rad`);
    await shoot(waiting[0], `car_parts_${name}_at_the_station`);
    let widest = 0;
    let shot = false;
    await watchTest(ride, 90, async (cars, trains) => {
        for (const [t, train] of trains.entries())
            for (const [k, swing] of train.swings.entries()) {
                widest = Math.max(widest, Math.abs(swing));
                const car = cars[t * train.swings.length + k];
                if (!shot && Math.abs(swing) > least && car !== undefined) {
                    shot = true;
                    await shoot(car, `car_parts_${name}_swung`, 12);
                }
            }
    });
    log(`${design}: swung up to`, widest.toFixed(3), "rad");
    expect(widest > least, `${design}'s ${name} swung, only ${widest.toFixed(3)} rad`);
}

// Coupled where the sleds say: a bobsleigh's sleds hang well under the track their cars follow, and each link's ends
// meet the points on the sleds either side of it where links couple on.
{
    const ride = await lay("Toxic Toboggan");
    expect(rides.setState(ride, "testing") == null, "tested");
    await park.advance(0.5);
    const r = tracked(rides.list()[ride], "the ride");
    expect(r.carsPerTrain > must(r.passengerCars, "passenger cars"), "Toxic Toboggan's sleds linked");
    const cars = r.cars.slice(0, r.carsPerTrain);
    // The sleds couple on low on them, under a metre over where they stand, not up at the track they hang from.
    const sledsCouple = cars
        .filter((_, k) => k % 2 === 0)
        .map((c) => must(c.couplesUp, "a coupling height") - (c.up ?? 0));
    log("Toxic Toboggan: sleds couple on", sledsCouple.map((h) => h.toFixed(3)).join(" "), "over where they stand");
    expect(
        sledsCouple.every((h) => h > 0 && h < 1),
        `Toxic Toboggan's sleds coupling on low on them, not ${sledsCouple.map((h) => h.toFixed(3))} up`,
    );
    let furthest = 0; // The furthest a link's ends stand above or below where the sleds either side couple on
    for (let k = 1; k + 1 < cars.length; k += 2) {
        const [ahead, link, behind] = [cars[k - 1], cars[k], cars[k + 1]].map((c) =>
            must(c.couplesUp, "a coupling height"),
        );
        furthest = Math.max(furthest, Math.abs(link - (ahead + behind) / 2));
    }
    log("Toxic Toboggan: links' ends stand up to", furthest.toFixed(3), "off where their sleds couple on");
    expect(furthest < 0.05, `Toxic Toboggan's links meet their sleds, not ${furthest.toFixed(3)} off them`);
    // From just over the sleds, as the trough's walls hide them from the side.
    await ui.click("bar/Top_Pause");
    const sled = cars[2];
    camera.look(sled, { col: sled.col + 0.3, row: sled.row + 0.3, up: (sled.up ?? 0) + 2.5 });
    await frames(5);
    await screenshot("car_parts_bobsleigh_links");
    await ui.click("bar/Top_Pause");
}
