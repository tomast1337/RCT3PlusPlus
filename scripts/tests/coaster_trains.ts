// SPDX-License-Identifier: GPL-3.0-or-later
//
// Lays a saved design the install offers for a corkscrew coaster on the game's blank landscape and tests it with
// no path to it: its train runs all the same. Closed, its window's number boxes change its trains and its cars,
// and the park runs as many trains as the window shows, with the cars drawn to match. Opened, the boxes no
// longer change it. A shuttle coaster laid the same way runs one train, and its window offers no more.
//
//   ./nob script scripts/tests/coaster_trains.ts -- --no-audio

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
    // The window opens on the custom track, the first row; the first saved design is a heading and two rows below
    // it, in the window's units (a button is 22 of them high).
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
    const ride = build.construction()!.ride;
    expect((rides.list()[ride].pieces ?? 0) > 0, `the ${kind} design laid`);
    build.stop();
    await ui.click("placing/close");
    await frames(5);
    return ride;
}

/** Clicks the up or down arrow of the number box `key` on the vehicles tab of the ride's window. */
async function step(ride: number, key: string, up: boolean): Promise<void> {
    rides.show(ride);
    await frames(5);
    await ui.click("info/Attraction_Vehicles");
    await frames(5);
    const box = ui.find(`info/body/${key}`);
    expect(box !== undefined, `the ${key} box`);
    const quarter = (box!.bottom - box!.top) / 4;
    await pointer.click(box!.right - 0.002, up ? box!.top + quarter : box!.bottom - quarter);
    await frames(10);
}

function shown(ride: number): string {
    const r = rides.list()[ride];
    return `${r.trains} trains of ${r.passengerCars} cars (${r.carsPerTrain} with links), ${r.running} run, ${r.moving} under way, ${r.cars?.length} drawn`;
}

const kTrains = "AttractionVehicleDialogLabelNumTrains";
const kCars = "AttractionVehicleDialogLabelNumCars";

// Tested with no path to it, its train runs.
const coaster = await lay("Corkscrew");
log("laid:", shown(coaster));
const refused = rides.setState(coaster, "testing");
expect(refused == null, `the coaster tested, not refused with ${refused}`);
await until(() => (rides.list()[coaster].moving ?? 0) > 0, 600, "its train under way with no path to it");
expect(rides.list()[coaster].simulated === 1, "the ride run once");
const station = rides.list()[coaster].entrance ?? rides.list()[coaster].cars![0];
camera.look(station, { col: station.col + 8, row: station.row + 14, up: 25 });
await frames(30);
await screenshot("tested");

// Closed, one more train: the park runs both, and draws both.
expect(rides.setState(coaster, "closed") == null, "the coaster closed");
await frames(5);
const before = rides.list()[coaster];
await step(coaster, kTrains, true);
let now = rides.list()[coaster];
log("one more train:", shown(coaster));
expect(now.trains === before.trains! + 1, "a train more");
expect(now.running === now.trains, "the park running each of them");
expect(now.cars?.length === now.trains! * now.carsPerTrain!, "each train's cars drawn");

// A car fewer, or one more where it has the fewest its kind allows.
await step(coaster, kCars, false);
if (rides.list()[coaster].passengerCars === now.passengerCars)
    await step(coaster, kCars, true);
const cars = rides.list()[coaster];
log("other cars:", shown(coaster));
expect(cars.passengerCars !== now.passengerCars, "the cars changed");
expect(cars.running === cars.trains && cars.trains === now.trains, "as many trains run");
expect(cars.cars?.length === cars.trains! * cars.carsPerTrain!, "the new cars drawn, the old ones gone");

// Tested again, its trains set off in turn.
expect(rides.setState(coaster, "testing") == null, "the coaster tested again");
await until(() => (rides.list()[coaster].moving ?? 0) > 0, 600, "its trains under way");
await frames(120);
await screenshot("trains");
expect(rides.list()[coaster].simulated === 1, "the ride still run once");

// Open, its trains stay as they are.
expect(rides.setState(coaster, "open") == null, `the coaster opened, not refused with ${rides.setState(coaster, "open")}`);
await frames(5);
now = rides.list()[coaster];
await step(coaster, kTrains, true);
expect(rides.list()[coaster].trains === now.trains, "no train added while open");

// A shuttle runs one train, however many are asked for.
const shuttle = await lay("Halfpipe");
log("shuttle laid:", shown(shuttle));
await step(shuttle, kTrains, true);
expect(rides.list()[shuttle].trains === 1, "the shuttle kept to one train");
expect(rides.setState(shuttle, "testing") == null, "the shuttle tested");
await until(() => (rides.list()[shuttle].moving ?? 0) > 0, 600, "the shuttle's train under way");
log("shuttle tested:", shown(shuttle));
expect(rides.list()[shuttle].running === 1, "one shuttle train run");
const end = rides.list()[shuttle].cars![0];
camera.look(end, { col: end.col + 8, row: end.row + 14, up: 25 });
await frames(60);
await screenshot("shuttle");
