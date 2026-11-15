// SPDX-License-Identifier: GPL-3.0-or-later
//
// Raised track costs more on rides whose file charges for height. In the game's Vanilla Hills, on open level
// grass the park owns, a corkscrew coaster's first piece is raised with Shift: the panel shows a higher price
// than on the ground, laying it takes that much from the park's building spending and demolishing it, before the
// ride ever opened, gives all of it back. An air powered coaster's file charges nothing for height, so its raised
// first piece costs what it does on the ground.
//
//   ./nob script scripts/tests/track_height_cost.ts -- --no-audio

export {};

await park.open("Campaigns/Base/VanillaHills.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "tools");
expect(park.cash() !== undefined, "a park with money");

// Level grass the park owns, with nothing standing within a few tiles.
const { width, height } = map.size();
const taken = [...map.objects(), ...map.paths()];
const clear = (col: number, row: number) => {
    for (let r = row - 3; r <= row + 3; ++r)
        for (let c = col - 3; c <= col + 3; ++c) {
            const t = map.tile(c, r);
            if (t === undefined || !t.owned || t.water !== undefined || new Set(t.corners).size !== 1) return false;
        }
    return !taken.some((o) => Math.abs(o.col - col) <= 4 && Math.abs(o.row - row) <= 4);
};
let site: [number, number] | undefined;
for (let row = 4; row < height - 4 && site === undefined; ++row)
    for (let col = 4; col < width - 4 && site === undefined; ++col) if (clear(col, row)) site = [col, row];
const [col, row] = must(site, "open level grass");
log("site", col, row);

/** Begins a custom track of `overlay`'s ride `symbol`, its first piece's ghost on the site; its cost there. */
async function begin(overlay: string, symbol: string): Promise<number> {
    expect(build.pick(overlay, symbol, "designs"), `a ${symbol} to build`);
    await until(() => ui.find("design_browser") !== undefined, 600, "the designs window");
    await frames(3);
    await ui.click("design_browser/content/build");
    await look_down(col, row, 40);
    pointer.move(...on_view(col, row));
    await frames(10);
    const c = must(build.construction(), "the construction panel");
    expect(c.ghost && !c.clashes, `the first piece placeable on the grass (${JSON.stringify(c)})`);
    return c.cost;
}

/** Raises the first piece with Shift, the pointer left at `top`; its cost there. */
let top: [number, number] = [0, 0];
async function raise(): Promise<number> {
    const [x, y] = on_view(col, row);
    const ground = build.construction()!.ghostAt!.height;
    pointer.shift(true);
    await frames(3);
    top = [x, y - 0.2];
    pointer.move(...top);
    await frames(5);
    pointer.shift(false);
    await frames(3);
    const c = build.construction()!;
    expect(c.ghostAt!.height > ground, `the first piece raised (${c.ghostAt!.height}, ${ground} on the ground)`);
    return c.cost;
}

const groundCost = await begin("tracks/coasters/Corkscrew/Corkscrew", "Corkscrew");
const price = await raise();
expect(price > groundCost, `the raised piece costs more (${price}, ${groundCost} on the ground)`);
await screenshot("raised_price"); // The panel shows the higher price
const spent = park.ledger().rideConstruction;
await pointer.click(...top);
await frames(20);
const ride = build.construction()!.ride;
expect(rides.list()[ride].pieces === 1, "the raised piece laid");
expect(
    park.ledger().rideConstruction - spent === price,
    `${price} paid, not ${park.ledger().rideConstruction - spent}`,
);
await screenshot("raised_laid"); // Its price floats up
await ui.click("placing/body/construction/demolish");
await frames(10);
expect(rides.list()[ride].pieces === 0, "the raised piece demolished");
expect(
    park.ledger().rideConstruction === spent,
    `${price} paid back, not ${spent + price - park.ledger().rideConstruction}`,
);
build.stop();
await frames(5);

const flat = await begin("tracks/coasters/AirPoweredRC/AirPoweredRC", "AirPoweredRC");
const raised = await raise();
log("prices", groundCost, price, flat, raised);
expect(raised === flat, `the air powered coaster's raised piece costs what it does on the ground (${raised}, ${flat})`);
