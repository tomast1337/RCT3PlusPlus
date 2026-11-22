// SPDX-License-Identifier: GPL-3.0-or-later
//
// Dynamic shadows hold still as the sun moves on: guests stand on the paths with the park paused while the sun is
// set on by a tenth of a degree a frame, past several of the steps the shadows are drawn at, a screenshot each
// frame. The shadows of the guests and the ride should look the same in every picture; a broken build loses them
// on the frame of each step and fades them back in over the next few, a blink about once a second in play.
//
//   ./nob script scripts/tests/shadow_steps.ts
//   ./nob script scripts/tests/shadow_steps.ts -- --renderer opengl

export {};

await park.open("Campaigns/Base/VanillaHills.dat");
await park.ready();
options.set("shadows", 2);
park.setTime(0.45);
expect(debug.use("add_guests", 60) > 0, "guests added");
await park.advance(40);

// Over the tile with the most guests, from above and to one side, so their shadows fall on the path.
const count = new Map<string, number>();
for (const p of people.list()) {
    if (p.at) {
        const tile = `${Math.round(p.at.col)},${Math.round(p.at.row)}`;
        count.set(tile, (count.get(tile) ?? 0) + 1);
    }
}
const busiest = must([...count.entries()].sort((a, b) => b[1] - a[1])[0], "guests on the paths");
const [col, row] = busiest[0].split(",").map(Number);
camera.look({ col, row, up: 0 }, { col: col + 6, row: row + 6, up: 14 });
keys.press("p"); // Paused, nothing moves but the sun
await frames(30);

const start = park.dayClock().time;
for (let i = 0; i < 24; ++i) {
    park.setTime(start + (i * 0.1) / 360);
    await frames(1);
    await screenshot(`sun_${i.toString().padStart(2, "0")}`);
}
expect(Math.abs(park.dayClock().time - (start + 2.3 / 360)) < 1e-4, "the day held where it was set while paused");
quit();
