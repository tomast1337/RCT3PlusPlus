// SPDX-License-Identifier: GPL-3.0-or-later
//
// A show ride's show: once its audience is seated and the ride sets off, the performers do their tricks in the
// order the show lists them, then go back to their idle until the next show. The ride stands in water, which
// fills its pool. Guests passing on the paths close by stop to watch the show and walk on when it ends, thinking
// well of it. Pictured during a trick and between shows. Give it a park the game saved with a show ride open to
// guests and paths near it:
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
expect(
    !map.pavings().some((p) => p.col === site.col && p.row === site.row),
    "the ride stands in water and nothing paves over its pool",
);
camera.look({ col: site.col, row: site.row }, { col: site.col + 6, row: site.row + 6, up: 14 });

const show = () => park.shows()[0];
const watching = () =>
    people.list().filter((p) => !p.staff && "state" in p && (p as WalkedPerson).lingering === "sight").length;
const kGreatEntertainer = 38; // The thought's number, as guests' windows list them
const thinking = () => people.list().filter((p) => !p.staff && p.thoughts.includes(kGreatEntertainer)).length;
// The next show from its start, so its watchers are seen to stop and walk on.
await park.advanceUntil(() => !show().running, 600, "a show to end");
const before = watching();
const thought_before = thinking();
await park.advanceUntil(() => show().running, 600, "the show starting");
await park.advanceUntil(() => show().trick !== null, 60, "a trick");
const trick = must(show().trick, "the trick playing");
log("trick", trick, "at", show().into.toFixed(1));
expect(trick === first.tricks[0], "the show opens with its first trick");
// A trick opens with the performers under the water; some way in they leap out of it.
await park.advanceUntil(() => show().into > 20, 60, "the performers leaping");
await frames(5);
await screenshot("show_trick");
const during = watching();
log("watching", before, "then", during);
expect(during > before, "passers-by stop to watch the show");
// A watcher seen from just behind, by the guest camera (started as the game does, by the cheat name), facing the
// show with the arena beyond them.
const watcher = must(
    people.list().find((p) => !p.staff && "state" in p && (p as WalkedPerson).lingering === "sight"),
    "a watcher",
).index;
people.rename(watcher, must(ui.text("CheatPeepName_PeepCam"), "the guest camera's cheat name"));
await until(() => camera.riding()?.eye !== undefined, 60, "the view from the watcher");
await ui.click("ride_camera/RidecamGUI_Type");
await frames(2);
expect(camera.riding()?.kind === "behind", "behind the watcher");
pointer.move(0.5, 0.5);
pointer.wheel(-1);
await frames(10);
await screenshot("show_watchers");
await ui.click("ride_camera/RidecamGUI_Exit");
await frames(5);
camera.look({ col: site.col, row: site.row }, { col: site.col + 6, row: site.row + 6, up: 14 });

await park.advanceUntil(() => !show().running, first.seconds + 60, "the show ending");
await park.advance(3);
expect(show().trick === null, "between shows the performers idle");
log("watching after", watching(), "great show thoughts", thought_before, "then", thinking());
expect(watching() < during, "the watchers walk on");
expect(thinking() > thought_before, "watchers think well of the show");
await frames(30);
await screenshot("show_idle");
quit();
