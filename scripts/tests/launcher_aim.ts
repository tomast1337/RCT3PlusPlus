// SPDX-License-Identifier: GPL-3.0-or-later
//
// Aimed launchers fire the way they are aimed: the park's display with the most aimed launchers, started at night and
// pictured from a little way off and above at a few moments while it plays, so the shells, beams or water show
// leaving their launchers at a slant rather than straight up. Give it a park the game saved with a display whose
// launchers are aimed (a show of cannons or fountains often is):
//
//   ./nob script scripts/tests/launcher_aim.ts -- --no-intro --no-audio --seed 1 --park PARK

export {};

await until(() => park.loaded(), 1200, "the park");
await frames(10);
const all = park.displays();
log(
    "displays",
    all.map(
        (d) => `${d.name}: ${d.kind}, ${d.launchers} launchers, ${d.aimed} aimed, ${d.keyed} keyed, ${d.moving} moving`,
    ),
);
let index = -1;
for (let i = 0; i < all.length; ++i)
    if (
        all[i].aimed + all[i].keyed > 0 &&
        (index < 0 || all[i].aimed + all[i].keyed > all[index].aimed + all[index].keyed)
    )
        index = i;
const show = must(all[index], "a display with aimed launchers in the park");
log("display", show.name, show.shots, "shots,", show.length.toFixed(1), "s");
park.setDayMode("night");
park.setTime(0);
await frames(5);
expect(park.startDisplay(index), "the display starts");
const centre = must(park.displays()[index].centre, "the middle of the launchers");
const tubes = must(park.displays()[index].tubes, "the launchers' tubes");
for (const t of tubes)
    log(
        "tube at",
        t.col.toFixed(1),
        t.row.toFixed(1),
        "points",
        t.way.map((v) => v.toFixed(2)),
    );
const slanted = tubes.filter((t) => t.way[1] < 0.95);
expect(slanted.length > 0, "an aimed launcher points off straight up");

// A picture every few seconds over the first part of the show: from behind an aimed launcher, looking the way it
// points, then from beside the launchers with the whole show in view.
const pictures = 6;
for (let i = 0; i < pictures && park.displays()[index].playing; ++i) {
    await frames(60 * 3);
    const t = slanted[i % slanted.length];
    const level = Math.hypot(t.way[0], t.way[2]) || 1;
    // A tile is about 4 m; stand 6 m behind it and 3 m up, looking 30 m along its way.
    const back = { col: t.col - (t.way[0] / level) * 1.5, row: t.row - (t.way[2] / level) * 1.5, up: t.up + 3 };
    const ahead = { col: t.col + t.way[0] * 7.5, row: t.row + t.way[2] * 7.5, up: t.up + t.way[1] * 30 };
    if (i % 2 === 0) camera.look(ahead, back);
    else camera.look({ ...centre, up: 8 }, { col: centre.col + 12, row: centre.row + 12, up: 20 });
    await frames(2);
    log("into the display", park.displays()[index].seconds.toFixed(1), "s");
    await screenshot(`aimed_${i}`);
}
park.stopDisplay();
quit();
