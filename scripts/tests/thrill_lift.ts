// SPDX-License-Identifier: GPL-3.0-or-later
//
// Lays the install's saved designs with a lift tower, a tilt coaster's and a sky swinger's, on the game's blank
// landscape and tests the tilt coaster with no path to it. The train rolls onto the lift's car at the tower's foot, the car
// carries it up, lets it go at the top and goes back down, and the train comes round to it again.
//
//   ./nob script scripts/tests/thrill_lift.ts -- --no-audio

export {};

/** Lays the saved design `design` of the coaster `kind` on a new blank landscape, and gives its place in rides.list(). */
async function lay(kind: string, design: string): Promise<number> {
    await park.open("Campaigns/Base/BlankLandscape.dat");
    await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "tools");
    expect(build.pick(`tracks/coasters/${kind}/${kind}`, kind, "designs"), `a ${kind} coaster to build`);
    await until(() => ui.find("design_browser") !== undefined, 600, "the designs window");
    await frames(3);
    const designs = build.construction()?.designs ?? [];
    const row = designs.findIndex((d) => d.includes(design));
    expect(row >= 0, `the ${design} design among ${JSON.stringify(designs)}`);
    // The window opens on the custom track, the first row; the saved designs follow a heading, each a row, in the
    // window's units (a button is 22 of them high).
    const list = must(ui.find("design_browser/content/list"), "design_browser/content/list shown");
    const button = must(ui.find("design_browser/content/build"), "design_browser/content/build shown");
    const unit = (button.bottom - button.top) / 22;
    await pointer.click(list.x, list.top + (18 + 16 * (2 + row)) * unit);
    await frames(5);
    await ui.click("design_browser/content/build");
    await frames(10);
    pointer.move(0.5, 0.45);
    await frames(30);
    const ride = build.construction()!.ride;
    await pointer.click(0.5, 0.45);
    await frames(30);
    expect((rides.list()[ride].pieces ?? 0) > 0 && build.construction() === undefined, `the ${design} design laid`);
    await frames(5);
    return ride;
}

type Lifted = { phase: string; aboard: number };

/** The lift car of ride `ride`, as drawn now. */
function lift_car(ride: number): MapPoint & { facing: number } {
    return must(
        rides.list()[ride].cars?.find((c) => c.lift),
        "the lift car drawn",
    );
}

/** Takes the picture `name` of ride `ride`'s lift car, from a little off to its side. */
async function picture(ride: number, name: string): Promise<void> {
    const car = lift_car(ride);
    camera.look(car, { col: car.col + 7, row: car.row + 10, up: (car.up ?? 0) + 3 });
    await frames(10);
    await screenshot(name);
}

function lifted(ride: number): Lifted | null {
    return rides.list()[ride].trainsAt?.[0]?.lift ?? null;
}

/** Tests the ride `ride`, watching its first train up its first lift tower and round to it again, with a picture `name`
 * at the foot, half way up, at the top and on the way down. */
async function ride_up(ride: number, name: string): Promise<void> {
    const laid = rides.list()[ride];
    log(`${name}:`, laid.track?.filter((p) => p.includes("Lift")).join(" "), `train ${laid.trainsAt?.[0]?.length}`);
    const refused = rides.setState(ride, "testing");
    expect(refused == null, `${name} tested, not refused with ${refused}`);
    await until(() => lifted(ride) !== null, 600, `${name}'s train on the lift's car`);
    await until(() => lifted(ride)?.phase === "Engaging", 900, `${name}'s train standing on the car`);
    log("standing at", JSON.stringify(lifted(ride)));
    await picture(ride, `${name}_bottom`);
    const bottom = lift_car(ride).up ?? 0;
    await until(() => lifted(ride)?.phase === "Rising", 600, `${name}'s car setting off`);
    await frames(300);
    const mid = lift_car(ride).up ?? 0;
    log("lift car up from", bottom, "to", mid);
    expect(mid > bottom + 1, `${name}'s car carrying the train up`);
    await picture(ride, `${name}_mid`);
    await until(
        () => ["AtTop", "Releasing", "Leaving"].includes(lifted(ride)?.phase ?? ""),
        1800,
        `${name}'s car at the top`,
    );
    await picture(ride, `${name}_top`);
    await until(() => lifted(ride) === null, 900, `${name}'s train let go`);
    await picture(ride, `${name}_drop`);
    log("let go at", JSON.stringify(rides.list()[ride].trainsAt?.[0]));
    // The car goes back down.
    await until(
        () => (rides.list()[ride].cars?.find((c) => c.lift)?.up ?? 1e9) < bottom + 0.5,
        900,
        `${name}'s car back down`,
    );
    // Tested, it runs round again without a stop, and the car takes it up once more.
    await until(() => lifted(ride) !== null, 3600, `${name}'s train round to the lift again`);
    log("round again:", JSON.stringify(rides.list()[ride].trainsAt?.[0]));
}

await ride_up(await lay("TiltCoaster", "ThrillLift Extreme"), "tilt");

// shortcut: the sky swinger's cars are read with no seats, so the park does not run it; it is only checked to stand
// its lift's car at the tower's foot. Upgrade to ride_up once its cars' seats are read.
{
    const ride = await lay("SkySwinger", "Stomach Turn");
    expect(rides.list()[ride].track?.some((p) => p.startsWith("LiftTop")) === true, "the swinger's lift tower laid");
    // Put to test, its trains are made ready, and with them its lift's car.
    expect(rides.setState(ride, "testing") == null, "the swinger tested");
    await frames(30);
    const car = lift_car(ride);
    log("swinger's lift car at", JSON.stringify(car));
    expect((car.up ?? 0) < 6, "the swinger's lift car standing at the tower's foot");
    await picture(ride, "swinger_bottom");
}
