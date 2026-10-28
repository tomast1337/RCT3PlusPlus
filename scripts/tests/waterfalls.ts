// SPDX-License-Identifier: GPL-3.0-or-later
//
// Waterfalls: a scenario's falls drawn from the park, seen from below; one made with the tool, cutting its
// spillway into the bank, and deleted again, putting the ground back.
//
//   ./nob script scripts/tests/waterfalls.ts -- --no-intro --no-audio --no-simulate --seed 1

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

await park.open("Campaigns/Base/Soaked/OasisOfFun.dat");
await until(() => ui.find("tools/Land/Land") !== undefined, 300, "the tool row");

const falls = map.waterfalls();
log("waterfalls", JSON.stringify(falls));
expect(falls.length === 6, "the scenario's six waterfalls");
for (const f of falls)
    expect(f.top > f.bottom && f.width > 0, "each to fall from a higher lake to a lower one");

// The tallest, from in front of where it lands and a little to the side.
const tall = falls.reduce((a, b) => (a.top - a.bottom >= b.top - b.bottom ? a : b));
const out = [tall.landCol - tall.col, tall.landRow - tall.row];
const span = Math.hypot(out[0], out[1]);
const dir = [out[0] / span, out[1] / span];
camera.look({ col: (tall.col + tall.landCol) / 2, row: (tall.row + tall.landRow) / 2, up: (tall.top - tall.bottom) / 2 }, { col: tall.landCol + dir[0] * 14 + dir[1] * 6, row: tall.landRow + dir[1] * 14 - dir[0] * 6, up: (tall.top - tall.bottom) * 0.6 });
await frames(10);
await screenshot("tallest");
await frames(30);
await screenshot("tallest_later");

// The waterfall tool: the same two lakes clicked again make a second fall over the same edge; one lake
// clicked twice makes none.
const first = falls[0];
const upper = { col: Math.floor(first.col - 0.5 * Math.sign(first.landCol - first.col)), row: Math.floor(first.row - 0.5 * Math.sign(first.landRow - first.row)) };
const lower = { col: Math.floor(first.landCol), row: Math.floor(first.landRow) };
log("lakes", JSON.stringify(map.tile(upper.col, upper.row)), JSON.stringify(map.tile(lower.col, lower.row)));
expect(Math.abs((map.tile(upper.col, upper.row)?.water ?? -1e9) - first.top) < 0.1 && map.tile(lower.col, lower.row)?.water !== undefined, "the first fall's lakes either side of it");
camera.look({ col: (upper.col + lower.col) / 2 + 0.5, row: (upper.row + lower.row) / 2 + 0.5 }, { col: (upper.col + lower.col) / 2 - 2, row: (upper.row + lower.row) / 2 - 10, up: 120 });
await frames(2);
if (!ui.find("tools/Land/Land_Waterfall"))
    await ui.click("tools/Land/Land");
await ui.click("tools/Land/Land_Waterfall");
function click_tile(col: number, row: number): Promise<void> {
    const at = map.screen(col, row);
    if (!at)
        throw new Error(`tile ${col}, ${row} is not in sight`);
    return pointer.click(at.x, at.y);
}
await click_tile(upper.col, upper.row);
await click_tile(upper.col, upper.row);
await frames(2);
expect(map.waterfalls().length === falls.length, "no fall from a lake into itself");
// The ground between the lakes, to see the spillway cut and put back.
const ground = (): string => {
    const out: number[] = [];
    for (let r = Math.min(upper.row, lower.row) - 6; r <= Math.max(upper.row, lower.row) + 6; ++r)
        for (let c = Math.min(upper.col, lower.col) - 6; c <= Math.max(upper.col, lower.col) + 6; ++c)
            out.push(...(map.tile(c, r)?.corners ?? []));
    return JSON.stringify(out);
};
const before = ground();
await click_tile(lower.col, lower.row); // Picked first this time; the fall still pours from the higher lake
await click_tile(upper.col, upper.row);
await frames(2);
const made = map.waterfalls();
log("made", JSON.stringify(made[made.length - 1]));
expect(made.length === falls.length + 1, "a new waterfall");
expect(Math.abs(made[made.length - 1].top - first.top) < 0.1 && Math.abs(made[made.length - 1].bottom - first.bottom) < 0.1, "it to join the same two lakes");
await screenshot("made");

// Unless the lower lake lies against the edge, the new fall cut a spillway into the bank, one tile out; deleting
// it with a right click on its water puts the ground back.
const fresh = made[made.length - 1];
const reach = Math.abs(lower.col - upper.col) + Math.abs(lower.row - upper.row);
log("reach", reach, "cut", ground() !== before);
expect(reach <= 1 || ground() !== before, "the spillway cut");
const middle = map.screen((fresh.col + fresh.landCol) / 2, (fresh.row + fresh.landRow) / 2);
if (!middle)
    throw new Error("the new fall is not in sight");
await pointer.click(middle.x, middle.y, "right");
await frames(2);
log("falls after deleting", map.waterfalls().length);
expect(map.waterfalls().length === falls.length, "the new fall deleted");
expect(ground() === before, "the ground put back");
await screenshot("deleted");
