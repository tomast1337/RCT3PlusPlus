// SPDX-License-Identifier: GPL-3.0-or-later
//
// Viewing galleries: those a park was saved with come back on their enclosure's edge, joined to a path, rated by
// the enclosure they look into, and guests pay to watch through them.
//
//   - Run Down Zoo: its galleries are found, one is shown in the park and its window opened; after a month
//     guests have watched at them and paid their tickets.
//   - Scrub Gardens: its gallery visitors goal reads the visitors its galleries have had, and gains on them once
//     its galleries, built but shut as the scenario starts, are opened.
//
//   ./nob script scripts/tests/viewing_galleries.ts -- --no-intro --no-audio --seed 1

export {};

const kMonth = 4 * 90; // Four weeks of the park's time
type Gallery = Extract<RideInfo, { kind: "gallery" }>;
const galleries = () => rides.list().filter((r): r is Gallery => r.kind === "gallery" && !r.demolished);

await park.open("Campaigns/Base/Wild/RunDownZoo.dat", { sandbox: false });
await park.ready();
await frames(10);
const found = galleries();
log("galleries", found.map((g) => `${g.name} panels ${g.panels.length} at ${JSON.stringify(g.panels[0])}`).join("; "));
expect(found.length === 5, `Run Down Zoo's five galleries, not ${found.length}`);
for (const g of found) {
    expect(g.panels.length > 0, `${g.name} with its panels`);
    expect(g.joined, `${g.name} joined to a path`);
}
const before = found.map((g) => g.customers ?? 0);
const shown = found.reduce((a, b) => (b.panels.length > a.panels.length ? b : a));
await park.advance(20);
for (const g of galleries())
    log(g.name, `excitement ${g.excitement?.toFixed(2) ?? "-"}, served from ${JSON.stringify(g.servedFrom)}`);
expect(
    galleries().some((g) => (g.excitement ?? 0) > 0),
    "some gallery rated above nothing",
);

const at = shown.panels[shown.panels.length >> 1];
await look_down(at.col, at.row, 30);
await frames(5);
await screenshot("gallery");
rides.show(shown.index);
await until(() => ui.find("info/Attraction_Customers") !== undefined, 120, "the gallery's window");
await ui.click("info/Attraction_Customers");
await frames(5);
await screenshot("gallery window");

await park.advance(kMonth);
const after = galleries();
const watched = after.reduce((n, g, i) => n + (g.customers ?? 0) - before[i], 0);
for (const g of after) log(g.name, `customers ${g.customers}, excitement ${g.excitement?.toFixed(2) ?? "-"}`);
expect(watched > 0, "guests watching at the galleries over a month");
await screenshot("gallery month");
await ui.click("info/close");

await park.open("Campaigns/Base/Wild/ScrubGardens.dat", { sandbox: false });
await park.ready();
await frames(10);
const visitors = () => park.goals().find((g) => g.kind === "EnclosureVisitorsCondition");
for (const g of galleries()) {
    const refused = rides.setState(g.index, "open");
    expect(refused == null, `${g.name} opened, not refused for ${refused}`);
}
await park.advance(90 / 7);
const start = must(visitors(), "Scrub Gardens' gallery visitors goal");
log("visitors goal", `${start.now ?? "-"} of ${start.target ?? "-"}`);
expect(start.judged && start.now !== null, "the gallery visitors goal reads a figure");
await park.advance(kMonth);
for (const g of galleries()) log(g.name, `customers ${g.customers}, excitement ${g.excitement?.toFixed(2) ?? "-"}`);
const end = visitors()!;
log("visitors goal after a month", `${end.now ?? "-"} of ${end.target ?? "-"}${end.met ? ", met" : ""}`);
expect((end.now ?? 0) > (start.now ?? 0), "the gallery visitors goal gaining over a month");
// A gallery onto healthy animals leaves its visitors pleased, as its satisfaction shows.
rides.show(galleries()[0].index);
await until(() => ui.find("info/Attraction_Customers") !== undefined, 120, "the gallery's window");
await ui.click("info/Attraction_Customers");
await frames(5);
await screenshot("pleased gallery window");
