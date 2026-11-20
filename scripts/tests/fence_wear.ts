// SPDX-License-Identifier: GPL-3.0-or-later
//
// Enclosure fences wearing out. In a park of the Wild! expansion, the Debug tab's knocks wear the straight panels
// of the enclosures' fences: a knock dents a panel, a second breaks a gap in it, and each shows so; corners do not
// wear. A park saved with a worn fence loads with it. The park's mechanics mend them from the paths beside, broken
// panels first. The player is told as a fence breaks and once an enclosure's is mended.
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

await park.advance(2); // The fences looked at once, as the park had them
const told = (key: string) => park.messages().some((m) => m.key === key);
expect(!told("EnclosureMaintenance_Broken"), "not told of fences as broken as they were loaded");
expect(debug.use("knock_fences", 60) === 60, "60 knocks, each wearing a panel");
await park.advanceUntil(() => told("EnclosureMaintenance_Broken"), 5, "the player told of a broken fence");
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

// The park's mechanics see to them, broken panels first, each from the path nearest it, and leave them as new.
const mechanics = () => staff().filter((s) => s.role === "mechanic");
log(`${mechanics().length} mechanics`);
expect(mechanics().length > 0, "mechanics in the park");
const broken_now = () => panels().filter((p) => wear(p) === "broken");
const near_gap = (m: Person) =>
    m.at !== null && broken_now().some((b) => Math.max(Math.abs(b.col - m.at!.col), Math.abs(b.row - m.at!.row)) <= 4);
await park.advanceUntil(
    () => mechanics().some((m) => m.job === "fix" && near_gap(m)),
    600,
    "a mechanic at work beside a broken fence",
);
const fixer = mechanics().find((m) => m.job === "fix" && near_gap(m))!;
// From behind the mechanic, looking over them at the gap.
const at = fixer.at!;
const gap_at = broken_now().reduce((a, b) =>
    Math.hypot(a.col - at.col, a.row - at.row) <= Math.hypot(b.col - at.col, b.row - at.row) ? a : b,
);
const away = { col: at.col - (gap_at.col + 0.5), row: at.row - (gap_at.row + 0.5) };
const length = Math.max(Math.hypot(away.col, away.row), 0.1);
camera.look(
    { col: (at.col + gap_at.col + 0.5) / 2, row: (at.row + gap_at.row + 0.5) / 2 },
    { col: at.col + (away.col / length) * 5, row: at.row + (away.row / length) * 5, up: 3 },
);
await frames(30);
await screenshot("fence_mending");
await park.advanceUntil(() => broken_now().length < broken.length, 600, "a broken panel mended");
log(`${broken.length - broken_now().length} broken panels mended`);
await park.advanceUntil(() => told("EnclosureMaintenance_Fixed"), 1200, "the player told of an enclosure mended");
await screenshot("fence_mended_message");
