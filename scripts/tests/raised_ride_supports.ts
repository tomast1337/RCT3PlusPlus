// SPDX-License-Identifier: GPL-3.0-or-later
//
// Raises a ride with Shift while placing it on the game's blank landscape. Its ghost stands on the scaffolding
// its own item names; once placed, with its doors, it stands on the columns of the path style its floor is
// paved in, the plain one with no path past its exit, and only under its rim, not under its middle.
//
//   ./nob script scripts/tests/raised_ride_supports.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => build.flatRides() !== undefined, 1200, "the build lists");
const ride = build.flatRides()!.find((r) => r.symbol === "Aviator");
expect(ride !== undefined, "the Aviator in the install");
const gate = map.paths().reduce((a, b) => (b.row > a.row ? b : a));
const col = gate.col + 4;
const row = gate.row - 14;
camera.look({ col: col + 3, row: row + 3 }, { col: col + 3, row: row + 18, up: 14 });
await frames(3);

/** The support pieces standing on the tiles the ride covers. */
const columns = () => map.objects().filter((o) => o.overlay.endsWith("Supports") && o.col >= col && o.col < col + ride!.width && o.row >= row && o.row < row + ride!.length);
const at = (c: number, r: number) => {
    const p = map.screen(c, r);
    expect(p !== undefined, `tile ${c},${r} in sight`);
    return p!;
};

expect(build.pick(ride!.overlay, ride!.symbol), "the Aviator picked");
const centre = at(col + ((ride!.width - 1) >> 1), row + ((ride!.length - 1) >> 1));
pointer.move(centre.x, centre.y);
await frames(3);
const ground = build.placing()!.site!.height;
pointer.shift(true);
await frames(2);
pointer.move(centre.x, centre.y - 0.1);
await frames(5);
const raised = build.placing()!;
log("raised from", ground, "to", raised.site?.height, raised.problem);
expect(raised.site!.height > ground + 4 && raised.problem === "none", `the ride raised and placeable, not ${raised.problem} at ${raised.site?.height}`);
expect(raised.ghost, "a ghost to show");
await screenshot("ghost_scaffolding");
await pointer.click(centre.x, centre.y - 0.1);
pointer.shift(false);
await frames(2);
for (const [step, c, r] of [["entrance", col, row + ride!.length], ["exit", col + ride!.width, row + ride!.length - 1]] as const) {
    const p = at(c, r);
    pointer.move(p.x, p.y);
    await frames(2);
    expect(build.placing()?.step === step && build.placing()?.problem === "none", `the ${step} placeable, not ${JSON.stringify(build.placing())}`);
    await pointer.click(p.x, p.y);
    await frames(2);
}
build.stop();
await frames(10);

const placed = columns();
log("columns", [...new Set(placed.map((o) => o.symbol))].join(", "));
expect(placed.length > 0, "columns under the raised ride");
expect(placed.every((o) => !o.symbol.startsWith("Scaff")), "no scaffolding left once placed");
expect(placed.every((o) => o.symbol.toLowerCase().startsWith("asph")), "the plain path style's columns under it");
const rim = (o: { col: number; row: number }) => o.col === col || o.row === row || o.col === col + ride!.width - 1 || o.row === row + ride!.length - 1;
expect(placed.every(rim), "columns under its rim only");
await screenshot("placed_path_columns");
