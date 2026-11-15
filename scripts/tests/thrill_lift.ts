// SPDX-License-Identifier: GPL-3.0-or-later
//
// Lays the install's saved designs with a lift tower, a tilt coaster's and a sky swinger's, on the game's blank
// landscape and tests each with no path to it. The train rolls onto the lift's car at the tower's foot, the car
// carries it up, lets it go at the top and goes back down, and the train comes round to it again.
// Then builds each kind's lift tower by hand from the construction panel's list, after a station: the foot is offered
// at the station's end, then only middles and the top; auto-complete closes the circuit, and the train rides the
// tower the same way.
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

/** Picks the piece the construction panel lists with a name matching `name`, going down the list from its first
 * row, and builds it with Enter when `build_it`; false when no row the list offers has it. */
async function pick_listed(name: RegExp, build_it: boolean): Promise<boolean> {
    const list = ui.find("placing/body/construction/list");
    if (list === undefined) return false;
    // A row pressed gives the list the keys. A second press on the picked row soon after the last builds its piece,
    // so the last press is waited out first.
    const since = Date.now();
    while (Date.now() - since < 500) await frames(1);
    await pointer.click(list.x, list.top + 0.03);
    await frames(2);
    keys.press("Home");
    await frames(2);
    for (let row = 0; row < 80 && !name.test(build.construction()?.piece ?? ""); ++row) {
        keys.press("Down");
        await frames(2);
    }
    if (!name.test(build.construction()?.piece ?? "")) return false;
    if (build_it) {
        keys.press("Enter");
        await frames(10);
    }
    return true;
}

/** Builds a `kind` coaster by hand on a new blank landscape: a station of `stations` pieces, a lift tower of `middles`
 * middles, then down from its top when it `falls` (points the track straight down), turned round and auto-completed
 * to the station. Gives its place in rides.list(). */
async function build_by_hand(
    kind: string,
    stations: number,
    middles: number,
    falls: boolean,
    name: string,
): Promise<number> {
    await park.open("Campaigns/Base/BlankLandscape.dat");
    await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "tools");
    expect(build.pick(`tracks/coasters/${kind}/${kind}`, kind, "designs"), `a ${kind} coaster to build`);
    await until(() => ui.find("design_browser") !== undefined, 600, "the designs window");
    await frames(3);
    await ui.click("design_browser/content/build");
    await frames(10);
    const ride = build.construction()!.ride;
    const { width, height } = map.size();
    const at = { col: Math.floor(width / 2) - 10, row: Math.floor(height / 2) };
    camera.look({ col: at.col + 8, row: at.row + 0.5 }, { col: at.col + 8, row: at.row + 30, up: 30 });
    await frames(5);
    const p = must(map.screen(at.col, at.row), `tile ${at.col}, ${at.row} in sight`);
    pointer.move(p.x, p.y);
    await frames(10);
    await pointer.click(p.x, p.y);
    await frames(30);
    const laid = () => rides.list()[ride].track ?? [];
    // The station, from its picture button, clicked once to pick it (once it is picked, a click builds it) and again
    // to build it.
    for (let tries = 0; tries < 4 * stations && laid().length < stations; ++tries) {
        for (let i = 0; i < 8; ++i) {
            const button = `placing/body/construction/special${i}`;
            if (ui.find(button) === undefined) continue;
            const before = laid().length;
            await ui.click(button);
            await frames(5);
            if (laid().length > before) break;
            if (/^station/i.test(build.construction()?.piece ?? "")) {
                await ui.click(button);
                await frames(10);
                break;
            }
        }
    }
    expect(laid().length >= stations, `${name}: a station of ${stations}, not ${JSON.stringify(laid())}`);
    // The tower's foot fits the station's end.
    expect(
        await pick_listed(/LiftBottom/i, false),
        `${name}: the lift's foot offered at the station's end, not ${build.construction()?.piece}`,
    );
    expect(build.construction()?.ghost === true, `${name}: the lift's foot shown where it goes`);
    await screenshot(`${name}_menu_foot`);
    expect(await pick_listed(/LiftBottom/i, true), `${name}: the lift's foot built`);
    expect(/LiftBottom/i.test(laid().at(-1) ?? ""), `${name}: the foot laid, not ${laid().at(-1)}`);
    // On an open tower only its middle and its top fit.
    for (let m = 0; m < middles; ++m) {
        expect(
            build.construction()?.offered === 2,
            `${name}: a middle or the top on the open tower, not ${build.construction()?.offered} pieces`,
        );
        expect(await pick_listed(/LiftMiddle/i, true), `${name}: middle ${m} built`);
    }
    expect(build.construction()?.offered === 2, `${name}: a middle or the top on the open tower`);
    expect(await pick_listed(/LiftTop/i, true), `${name}: the lift's top built`);
    expect(/LiftTop/i.test(laid().at(-1) ?? ""), `${name}: the top laid, not ${laid().at(-1)}`);
    log(`${name} by hand:`, laid().join(" "));
    // Down from a falling top, straight down to near the ground, then easing to level, each picked and then built.
    const slope = async (button: string): Promise<boolean> => {
        const before = laid().length;
        const path = `placing/body/construction/AttractionEditor_Icon${button}`;
        if (ui.find(path) === undefined) return false;
        await ui.click(path);
        await frames(5);
        if (laid().length === before && build.construction()?.ghost === true) {
            await ui.click(path);
            await frames(10);
        }
        return laid().length > before;
    };
    if (falls) {
        for (let i = 0; i < 20 && (build.construction()?.ghostAt?.height ?? 0) > 14; ++i)
            expect(await slope("SlopeD3"), `${name}: a vertical piece down from the top`);
        for (const button of ["SlopeD2", "SlopeD1", "Slope0"])
            expect(await slope(button), `${name}: a piece ${button} on the way down`);
    }
    // The track comes down heading on from the tower: turned round beside it, and back past the station, where
    // auto-complete has only a turn left to make.
    for (const button of ["CurveR2", "CurveR2"]) expect(await slope(button), `${name}: a piece ${button} turning back`);
    for (let i = 0; i < 40 && (build.construction()?.ghostAt?.col ?? 0) > at.col - 2; ++i)
        expect(await slope("Curve0"), `${name}: a straight piece back past the station`);
    // Auto-complete finds a way down from the top round to the station, and lays it on a second press.
    await ui.click("placing/body/construction/complete");
    await frames(1);
    await until(() => build.construction()?.completing === false, 900, `${name}: auto-complete done`);
    expect(
        (build.construction()?.proposed ?? 0) > 0,
        `${name}: a way found round to the station, not ${build.construction()?.complaint}`,
    );
    await ui.click("placing/body/construction/complete");
    await frames(10);
    log(`${name} completed with ${laid().length} pieces`);
    build.stop();
    if (ui.find("placing/close") !== undefined) await ui.click("placing/close");
    await frames(10);
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
    // The view looks at the ground, so it looks past the car at the ground beyond it, from a little above and
    // off to its side, the car on the line between.
    const up = car.up ?? 0;
    const side = 6;
    const above = 12;
    const beyond = (side * up) / above;
    camera.look(
        { col: car.col - beyond, row: car.row - 1.5 * beyond },
        { col: car.col + side, row: car.row + 1.5 * side, up: up + above },
    );
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
    // Laid and not yet run, its lift's car already stands at the tower's foot.
    await until(() => rides.list()[ride].cars?.some((c) => c.lift) === true, 120, `${name}'s lift car drawn`);
    const laid_car = lift_car(ride);
    expect((laid_car.up ?? 0) < 6, `${name}'s lift car standing at the foot once laid, not ${laid_car.up} m up`);
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

await ride_up(await lay("SkySwinger", "Stomach Turn"), "swinger");

await ride_up(await build_by_hand("TiltCoaster", 6, 3, true, "tilt_hand"), "tilt_hand");

await ride_up(await build_by_hand("SkySwinger", 4, 2, false, "swinger_hand"), "swinger_hand");
