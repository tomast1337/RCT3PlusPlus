// SPDX-License-Identifier: GPL-3.0-or-later
//
// Lasers that draw pictures trace them in a park's display: the park's first display with drawing lasers, started at
// night. While a drawing laser's beam leaves spots on the ground, the camera looks down on them from a little way off,
// at a few moments, to judge whether the drawings read; now and then from farther back with the launchers in view.
// Drawings thrown at open sky leave no spot, so while lasers shine and nothing has landed yet, the beams are pictured
// from afar instead. Give it a park the game saved with a display of drawing lasers:
//
//   ./nob script scripts/tests/laser_drawings.ts -- --no-intro --no-audio --seed 1 --park PARK

export {};

await until(() => park.loaded(), 1200, "the park");
await frames(10);
const index = park.displays().findIndex((d) => d.drawings > 0);
const show = must(park.displays()[index], "a display with drawing lasers in the park");
log("display", show.name, show.shots, "shots,", show.drawings, "drawing lasers,", show.length.toFixed(1), "s");
park.setDayMode("night");
park.setTime(0);
await frames(5);
expect(park.startDisplay(index), "the display starts");
const now = () => park.displays()[index];

let pictures = 0;
let beams = 0;
let mostSpots = 0;
while (pictures < 6 && now().playing) {
    await frames(30);
    const d = now();
    if (!d.playing) break;
    mostSpots = Math.max(mostSpots, d.spots);
    const centre = must(d.centre, "the middle of the launchers");
    if (d.picture) {
        // Let the trace run on, so most of the picture is down.
        await frames(40);
        const picture = now().picture;
        if (!picture) continue;
        log("into the display", now().seconds.toFixed(1), "s, lasers shining", now().shining, "spots", now().spots);
        if (pictures % 3 === 2)
            camera.look({ ...picture, up: 0 }, { col: centre.col + 10, row: centre.row + 10, up: 25 });
        else camera.look({ ...picture, up: 0 }, { col: picture.col + 3, row: picture.row + 3, up: 12 });
        await frames(2);
        await screenshot(`drawings_${pictures++}`);
        // On to the next picture, past the spots of this one.
        await frames(60 * 2);
    } else if (d.shining > 0 && mostSpots === 0 && beams < 2) {
        log("into the display", d.seconds.toFixed(1), "s, lasers shining", d.shining, "with no spot down");
        camera.look({ ...centre, up: 20 }, { col: centre.col + 15, row: centre.row + 15, up: 35 });
        await frames(2);
        await screenshot(`beams_${beams++}`);
        await frames(60);
    }
}
log("most spots lit at once", mostSpots, mostSpots === 0 ? "(the drawings meet nothing within reach)" : "");
expect(pictures > 0 || beams > 0, "pictures of the drawing lasers");
park.stopDisplay();
await frames(2);
expect(now().spots === 0, "no spot is left once the display stops");
quit();
