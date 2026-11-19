// SPDX-License-Identifier: GPL-3.0-or-later
//
// Firework bursts light the park round them at night: a display the park saved, started from the Debug tab late in
// the evening, seen from above and to one side, so the ground, paths and water round the launchers show in each
// picture as the shells burst over them and fade. Give it a park the game saved with a display:
//
//   ./nob script scripts/tests/firework_light.ts -- --no-intro --no-audio --seed 1 --park PARK

export {};

await until(() => park.loaded(), 1200, "the park");
await frames(10);
must(park.displays()[0], "a firework display in the park");
park.setTime(23 / 24);
await frames(5);
expect(debug.use("start_display", 0) === 1, "the Debug tab starts the display");
const centre = must(park.displays()[0].centre, "the middle of the launchers");
camera.look({ ...centre, up: 0 }, { col: centre.col + 14, row: centre.row + 14, up: 45 });
const bursts = () => map.effects().filter((e) => Math.hypot(e.col - centre.col, e.row - centre.row) < 40 && e.live > 0);
await until(() => bursts().length > 0, 600, "shells going up");

// A picture every two seconds into the show, with how many sparks glow then.
for (let shot = 0; shot < 8 && park.displays()[0].playing; ++shot) {
    await frames(60 * 2);
    const live = bursts().reduce((sum, e) => sum + e.live, 0);
    log("into the display", park.displays()[0].seconds.toFixed(1), "s, effects", bursts().length, "sparks", live);
    await screenshot(`ground_${shot}`);
}
expect(park.displays()[0].playing, "still playing after the pictures");
quit();
