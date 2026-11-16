// SPDX-License-Identifier: GPL-3.0-or-later
//
// Guests using what stands by the paths, on the game's blank landscape. Beside the road in from the entrance:
// a drink stall, a cash machine, and a small fire on the grass next to the road. Guests dropped on the paths
// then:
//
//   - buy drinks and, with no bin anywhere, drop the empties they carry too long, so litter lies on the paths;
//   - put their empties in a bin once one stands on the road;
//   - short of cash, draw more at the cash machine;
//   - stop now and then to watch the fire, though it stands beside the road, not on it;
//   - know the fire no more once it is pulled down.
//
//   ./nob script scripts/tests/path_uses.ts -- --no-audio

export {};

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(
    () =>
        ui.find("tools/Paths/Obj_Paths") !== undefined &&
        build.stalls() !== undefined &&
        build.facilities() !== undefined,
    1200,
    "the build lists",
);
const gate = map.paths().reduce((a, b) => (b.row > a.row ? b : a));
const roadAt = (row: number) =>
    must(
        map.paths().find((p) => p.col === gate.col && p.row === row),
        `the road at row ${row}`,
    );

/** Places whatever is picked on tile (`col`, `row`), looking at it from the south first. */
async function place(what: string, col: number, row: number): Promise<void> {
    camera.look({ col: col + 0.5, row: row + 0.5 }, { col: col + 0.5, row: row + 10.5, up: 25 });
    await frames(3);
    const spot = must(map.screen(col, row), `tile ${col},${row} in sight`);
    pointer.move(spot.x, spot.y);
    await frames(3);
    expect(
        build.placing()?.problem === "none",
        `${what} placeable at ${col},${row}, not ${JSON.stringify(build.placing())}`,
    );
    await pointer.click(spot.x, spot.y);
    await frames(3);
    build.stop();
    if (ui.find("info") !== undefined) await ui.click("info/close");
    await frames(1);
}

// A drink stall east of the road, turned to face it.
const road = roadAt(gate.row - 4);
const drinks = must(
    build.stalls()!.find((s) => s.symbol.toLowerCase().includes("drink") || s.symbol.toLowerCase().includes("cola")),
    "a drink stall to build",
);
log("stall", drinks.symbol, drinks.goods.map((g) => g.item).join(", "));
camera.look({ col: road.col + 0.5, row: road.row + 0.5 }, { col: road.col + 0.5, row: road.row + 10.5, up: 25 });
await frames(3);
expect(build.pick(drinks.overlay, drinks.symbol), `${drinks.symbol} picked`);
const stallSpot = must(map.screen(road.col + 1, road.row), "the stall's tile in sight");
pointer.move(stallSpot.x, stallSpot.y);
await frames(3);
for (let turn = 0; turn < 4 && build.placing()?.site?.turns !== 1; ++turn) {
    await ui.click("placing/IconAttractionEditorRotate");
    pointer.move(stallSpot.x, stallSpot.y);
    await frames(3);
}
expect(build.placing()?.problem === "none", `the stall placeable, not ${JSON.stringify(build.placing())}`);
await pointer.click(stallSpot.x, stallSpot.y);
await frames(3);
build.stop();
if (ui.find("info") !== undefined) await ui.click("info/close");
const stall = stalls.list().findIndex((s) => s.symbol === `${drinks.symbol}:sta`);
expect(stall >= 0, "the stall in the park");
expect(stalls.setState(stall, "open") == null, "the stall open");

// The smallest cash machine (kind 12) east of the road, opening onto it.
const machine = must(
    build
        .facilities()!
        .filter((f) => f.kind === "cash machine")
        .sort((a, b) => a.width * a.length - b.width * b.length)[0],
    "a cash machine to build",
);
const machineRow = gate.row - 11;
expect(build.pick(machine.overlay, machine.symbol), `${machine.symbol} picked`);
await place("the cash machine", gate.col + 1, machineRow);
const cashMachine = rides.list().findIndex((r) => r.symbol === `${machine.symbol}:sat`);
expect(cashMachine >= 0, "the cash machine in the park");
expect(rides.setState(cashMachine, "open") == null, "the cash machine open");

// A small fire on the grass west of the road.
const fireRow = gate.row - 14;
roadAt(fireRow);
expect(build.pick("Style/Vanilla/Style", "SmallFire"), "the small fire in the scenery lists");
await place("the fire", gate.col - 1, fireRow);
expect(
    park.pathItems().some((i) => i.kind === "sight"),
    "the fire known to guests",
);

park.setWeather("sunny");
park.setTime(0.4);
park.dropParties(100, 3);
/** Daylight for a picture, whatever time the park has run to. */
const daylight = () => {
    park.setWeather("sunny");
    park.setTime(0.5);
};
/** Runs the park until `check` holds, kept dry: in the rain guests with nothing to shelter under soon go home. */
async function dryUntil(check: () => boolean, seconds: number, what: string): Promise<void> {
    for (let ran = 0; !check(); ran += 5) {
        expect(ran < seconds, `${what} within ${seconds} park seconds`);
        park.setWeather("sunny");
        await park.advance(5);
    }
}

// With no bin, litter on the paths: each guest's pockets hold two empties, and the third is dropped.
await dryUntil(() => park.litter().length > 0, 3600, "litter dropped");
const piece = park.litter()[0];
log("litter", park.litter().length, "first at", piece.col.toFixed(1), piece.row.toFixed(1));
daylight();
camera.look({ col: piece.col, row: piece.row }, { col: piece.col + 1, row: piece.row + 3, up: 4 });
await frames(10);
await screenshot("path_uses_litter");

// One bin on the road, a little further in: a guest hands their rubbish over on its tile.
const binTile = roadAt(gate.row - 7);
expect(build.pick("Style/Vanilla/Style", "Bin"), "the bin in the scenery lists");
await place("the bin", binTile.col, binTile.row);
expect(
    park.pathItems().some((i) => i.kind === "bin"),
    "the bin known to guests",
);
const onTile = (p: { col: number; row: number }, col: number, row: number) =>
    Math.floor(p.col) === col && Math.floor(p.row) === row;
const binning = () =>
    people
        .list()
        .find(
            (p): p is WalkedPerson =>
                p.state !== undefined && p.gesture === "handticket" && onTile(p, binTile.col, binTile.row),
        );
await dryUntil(() => binning() !== undefined, 1800, "a guest putting their empty in the bin");
const binner = must(binning(), "a guest at the bin");
daylight();
camera.look({ col: binner.col, row: binner.row, up: 0.8 }, { col: binner.col + 1.2, row: binner.row + 1.5, up: 2.5 });
await frames(4);
await screenshot("path_uses_bin");

// Short of cash, guests draw more.
log("guests short of cash", park.setGuestCash(100));
await dryUntil(() => (rides.list()[cashMachine].customers ?? 0) > 0, 1800, "a guest at the cash machine");
log("cash machine customers", rides.list()[cashMachine].customers);
camera.look(
    { col: gate.col + 1.5, row: machineRow + 0.5, up: 0.5 },
    { col: gate.col - 2.5, row: machineRow + 3.5, up: 5 },
);
daylight();
await frames(10);
await screenshot("path_uses_cash");

// The fire beside the road watched.
const watcher = () => people.list().find((p): p is WalkedPerson => p.state !== undefined && p.lingering === "sight");
await dryUntil(() => watcher() !== undefined, 1800, "a guest watching the fire");
const w = must(watcher(), "a guest watching the fire");
daylight();
camera.look({ col: w.col, row: w.row, up: 0.8 }, { col: w.col + 1.5, row: w.row + 1.5, up: 2.5 });
await frames(10);
await screenshot("path_uses_fire");

// Pulled down from its window, the fire is at once nothing guests know.
const fire = must(
    park.pathItems().find((i) => i.kind === "sight"),
    "the fire among the path items",
);
park.showObject(fire.object);
await frames(3);
await ui.click("info/Attraction_General_Delete");
await frames(3);
expect(!park.pathItems().some((i) => i.object === fire.object), "the fire gone from the path items");
quit(0);
