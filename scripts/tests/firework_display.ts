// SPDX-License-Identifier: GPL-3.0-or-later
//
// A firework display the park saved, put on at its time: just before it, nothing goes up; at it, the message
// tells the player, shells rise from the launchers and burst over them, and guests near it stop to watch. When
// it is over they walk on. Pictured from below the bursts, at night. Then the Debug tab's buttons start it at
// once and stop it. Give it a park the game saved with a display scheduled in its current month:
//
//   ./nob script scripts/tests/firework_display.ts -- --no-intro --no-audio --seed 1 --park PARK

export {};

await until(() => park.loaded(), 1200, "the park");
await frames(10);
const shows = park.displays();
log(
    "displays",
    shows.map(
        (d) => `${d.name}: ${d.shots} shots from ${d.launchers} launchers at ${d.start} min, ${d.length.toFixed(1)} s`,
    ),
);
const show = must(shows[0], "a firework display in the park");
expect(show.launchers > 0 && show.shots > 0, "the display has launchers and shots");

// A minute before its start: nothing yet.
park.setTime((show.start - 1) / (24 * 60));
await frames(5);
expect(!park.displays()[0].playing, "not before its time");
const watching = () =>
    people.list().filter((p) => !p.staff && "state" in p && (p as WalkedPerson).lingering === "sight").length;
const before = watching();

// Its start comes in the park's own time.
await park.advanceUntil(() => park.displays()[0].playing, 600, "the display starting");
expect(
    park.messages().some((m) => m.key === "FireworkDisplay_AboutToStart" && m.name === show.name),
    "a message tells of the display",
);
const centre = must(park.displays()[0].centre, "the middle of the launchers");
// From the ground a little way off, looking up into the sky the shells burst in.
camera.look({ ...centre, up: 30 }, { col: centre.col + 9, row: centre.row + 9, up: 2 });
const fireworks = () =>
    map.effects().filter((e) => Math.hypot(e.col - centre.col, e.row - centre.row) < 40 && e.live > 0);
await until(() => fireworks().length > 0, 600, "shells going up");
await frames(60 * 6);
const high = Math.max(...fireworks().map((e) => e.up));
log("effects", fireworks().length, "highest", high.toFixed(1));
await screenshot("display_bursts");

// Guests near it watch.
await park.advance(5);
const during = watching();
log("watching", before, "then", during);
expect(during > before, "guests stop to watch");
await frames(60 * 4);
await screenshot("display_later");
// Through the rest of it, a picture every few seconds.
for (let shot = 0; shot < 6 && park.displays()[0].playing; ++shot) {
    await frames(60 * 6);
    log("into the display", park.displays()[0].seconds.toFixed(1), "s, effects", fireworks().length);
    await screenshot(`display_${shot}`);
}

// It ends some seconds after its last shot, and the watchers walk on.
await park.advanceUntil(() => !park.displays()[0].playing, 600, "the display ending");
await park.advance(2);
log("watching after", watching());
expect(watching() < during, "the watchers walk on");

// The Debug tab starts it at once, out of its time, and stops it.
expect(debug.use("start_display", 0) === 1, "the Debug tab starts the display");
expect(park.displays()[0].playing, "playing at once");
expect(debug.use("start_display", shows.length) === 0, "no display past the park's last");
expect(debug.use("stop_display") === 1, "the Debug tab stops it");
expect(!park.displays()[0].playing, "stopped");
expect(debug.use("stop_display") === 0, "nothing left to stop");
quit();
