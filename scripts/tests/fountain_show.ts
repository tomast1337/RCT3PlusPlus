// SPDX-License-Identifier: GPL-3.0-or-later
//
// A fountain show the park saved: a display whose shots are water jets. Started by day, the jets shoot from
// their launchers along their patterns, guests near stop to watch, and when it ends they walk on thinking well of
// the water show. Give it a park the game saved with a water jet display:
//
//   ./nob script scripts/tests/fountain_show.ts -- --no-intro --no-audio --seed 1 --park PARK

export {};

await until(() => park.loaded(), 1200, "the park");
await frames(10);
const shows = park.displays();
log(
    "displays",
    shows.map((d) => `${d.name} (${d.kind}): ${d.shots} shots from ${d.launchers} launchers`),
);
// The shortest fountain show, so the script sees it end.
const water = shows
    .map((d, i) => ({ d, i }))
    .filter(({ d }) => d.kind === "water")
    .sort((a, b) => a.d.length - b.d.length);
const { d: show, i: index } = must(water[0], "a fountain show in the park");
// By day, so the water shows and the calendar starts no display over this one.
park.setDayMode("day");
park.setTime(0.5);
await frames(5);
park.stopDisplay();
const watching = () =>
    people.list().filter((p) => !p.staff && "state" in p && (p as WalkedPerson).lingering === "sight").length;
const before = watching();
// Started once to find the middle of its jets, and pictured stopped first.
expect(park.startDisplay(index), "the fountain show starts");
const centre = must(park.displays()[index].centre, "the middle of the jets");
park.stopDisplay();
camera.look(centre, { col: centre.col + 3, row: centre.row + 3, up: 45 });
await frames(120);
await screenshot("fountain_before");
expect(park.startDisplay(index), "the fountain show starts again");
await park.advance(4);
await frames(30);
await screenshot("fountain_jets");
camera.look(centre, { col: centre.col + 5, row: centre.row + 2, up: 16 });
await park.advance(6);
await frames(30);
const during = watching();
log("watching", before, "then", during);
expect(during > before, "guests stop to watch");
await screenshot("fountain_later");

await park.advanceUntil(() => !park.displays()[index].playing, show.length + 60, "the fountain show ending");
await park.advance(2);
const kWaterShowGood = 187; // The thought's number, as guests' windows list them
const thoughts = people.list().filter((p) => !p.staff && p.thoughts.includes(kWaterShowGood)).length;
log("watching after", watching(), "water show thoughts", thoughts);
expect(watching() < during, "the watchers walk on");
expect(thoughts > 0, "watchers think well of the water show");
quit();
