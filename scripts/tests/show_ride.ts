// SPDX-License-Identifier: GPL-3.0-or-later
//
// A show ride's show: once its audience is seated and the ride sets off, the performers do their tricks in the
// order the show lists them, then go back to their idle until the next show. Pictured during a trick and between
// shows. Give it a park the game saved with a show ride open to guests:
//
//   ./nob script scripts/tests/show_ride.ts -- --no-intro --no-audio --seed 1 --park PARK

export {};

await until(() => park.loaded(), 1200, "the park");
await frames(10);
const shows = park.shows();
log(
    "shows",
    shows.map((s) => `${s.name}: ${s.tricks.join(", ")} (${s.seconds.toFixed(1)} s)`),
);
expect(
    park.displays().every((d) => d.name !== "%ME%" || d.shots > 0),
    "a show ride's own timeline is not one of the park's displays",
);
const first = must(shows[0], "a show ride in the park");
expect(first.tricks.length > 0, "the show has tricks");
const ride = must(
    rides.list().find((r) => r.index === first.ride),
    "the show's ride",
);
const site = must(ride.site, "where the ride stands");
camera.look({ col: site.col, row: site.row }, { col: site.col + 6, row: site.row + 6, up: 30 });

const show = () => park.shows()[0];
await park.advanceUntil(() => show().running, 600, "the show starting");
await park.advanceUntil(() => show().trick !== null, 60, "a trick");
const trick = must(show().trick, "the trick playing");
log("trick", trick, "at", show().into.toFixed(1));
expect(trick === first.tricks[0], "the show opens with its first trick");
await frames(60);
await screenshot("show_trick");

await park.advanceUntil(() => !show().running, first.seconds + 60, "the show ending");
await park.advance(3);
expect(show().trick === null, "between shows the performers idle");
await frames(30);
await screenshot("show_idle");
quit();
