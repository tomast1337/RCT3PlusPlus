// SPDX-License-Identifier: GPL-3.0-or-later
//
// Waterfalls: a scenario's falls drawn from the park, seen from below.
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
