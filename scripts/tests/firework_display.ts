// SPDX-License-Identifier: GPL-3.0-or-later
//
// A firework display the park saved, put on at its time: just before it, nothing goes up; at it, the message
// tells the player, shells rise from the launchers and burst over them, and guests near it stop to watch. When
// it is over they walk on. Pictured from below the bursts, at night. Give it a park the game saved with a
// display scheduled in its current month:
//
//   ./nob script scripts/tests/firework_display.ts -- --no-intro --no-audio --seed 1 --park PARK

export {};

await until(() => park.loaded(), 1200, "the park");
await frames(10);
const shows = park.displays();
log(
    "displays",
    shows.map((d) => `${d.name}: ${d.shots} shots from ${d.launchers} launchers at ${d.start} min`),
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
camera.look(centre, { col: centre.col + 25, row: centre.row + 25 });
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

// It ends some seconds after its last shot, and the watchers walk on.
await park.advanceUntil(() => !park.displays()[0].playing, 600, "the display ending");
await park.advance(2);
log("watching after", watching());
expect(watching() < during, "the watchers walk on");
quit();
