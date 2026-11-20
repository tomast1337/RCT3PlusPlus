// SPDX-License-Identifier: GPL-3.0-or-later
//
// Enclosure fences wearing out. In a park of the Wild! expansion, the Debug tab's knocks wear the straight panels
// of the enclosures' fences: a knock dents a panel, a second breaks a gap in it, and each shows so; corners do not
// wear. A park saved with a worn fence loads with it.
//
//   RCT3PP_DATA=<an empty folder> ./nob script scripts/tests/fence_wear.ts -- --no-audio

export {};

await park.open("Campaigns/Base/Wild/ScrubGardens.dat");
await until(() => ui.find("tools/Land/Land") !== undefined && build.flatRides() !== undefined, 1200, "the build lists");
const panels = () =>
    map.objects().filter((o) => o.overlay.startsWith("Enclosures/Fences/") && o.symbol.endsWith("Edge"));
const wear = (o: { col: number; row: number }) => map.tile(o.col, o.row)?.fence;
expect(panels().length > 50, `the enclosures fenced, not ${panels().length} panels`);
// The park was saved with one panel already dented.
const loaded = panels().filter((p) => wear(p) !== "sound");
expect(
    loaded.length > 0 && loaded.every((p) => wear(p) === "damaged"),
    `the dented panel the park was saved with, not ${loaded.length} worn`,
);

expect(debug.use("knock_fences", 60) === 60, "60 knocks, each wearing a panel");
await frames(30);
const worn = panels().filter((p) => wear(p) !== "sound");
const broken = worn.filter((p) => wear(p) === "broken");
log(`${worn.length} panels worn, ${broken.length} of them broken`);
expect(broken.length > 0 && worn.length > broken.length, "some panels dented, some broken through");
const corners = map.objects().filter((o) => o.overlay.startsWith("Enclosures/Fences/") && !o.symbol.endsWith("Edge"));
expect(
    corners.every((c) => panels().some((p) => p.col === c.col && p.row === c.row) || wear(c) === "sound"),
    "no corner worn",
);

// A broken panel with a dented one near it, if any, both in view.
const gap =
    broken.find((b) =>
        worn.some((w) => wear(w) === "damaged" && Math.abs(w.col - b.col) + Math.abs(w.row - b.row) <= 6),
    ) ?? broken[0];
camera.look({ col: gap.col + 0.5, row: gap.row + 0.5 }, { col: gap.col + 4.5, row: gap.row + 5.5, up: 5 });
await frames(60);
await screenshot("fence_worn");
