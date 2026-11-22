// SPDX-License-Identifier: GPL-3.0-or-later
//
// Lasers in a park's display shine from their launchers: the park's first display with lasers, started at
// night and seen from a little way off and above, so the beams show against the night sky. A picture is
// taken at a few moments while lasers shine, to judge their way, colour, motion and brightness. Give it a park the
// game saved with a laser display:
//
//   ./nob script scripts/tests/laser_display.ts -- --no-intro --no-audio --seed 1 --park PARK

export {};

await until(() => park.loaded(), 1200, "the park");
await frames(10);
log(
    "displays",
    park.displays().map((d) => `${d.name}: ${d.shots} shots, ${d.lasers} lasers`),
);
const index = park.displays().findIndex((d) => d.lasers > 0);
const show = must(park.displays()[index], "a laser display in the park");
log("display", show.name, show.shots, "shots,", show.length.toFixed(1), "s");
park.setDayMode("night");
park.setTime(0);
await frames(5);
expect(park.startDisplay(index), "the display starts");
const centre = must(park.displays()[index].centre, "the middle of the launchers");
camera.look({ ...centre, up: 20 }, { col: centre.col + 15, row: centre.row + 15, up: 35 });
const shining = () => park.displays()[index].shining;

// A picture a second and a half apart while lasers shine, waiting through gaps between them.
let pictures = 0;
while (pictures < 6 && park.displays()[index].playing) {
    await until(() => shining() > 0 || !park.displays()[index].playing, 60 * 120, "a laser shining");
    if (!park.displays()[index].playing) break;
    await frames(60 * 1.5);
    if (shining() === 0) continue;
    log("into the display", park.displays()[index].seconds.toFixed(1), "s, lasers shining", shining());
    await screenshot(`lasers_${pictures++}`);
}
expect(pictures > 0, "pictures of lasers shining");
park.stopDisplay();
await frames(2);
expect(park.displays()[index].shining === 0, "no laser shines once the display stops");
quit();
