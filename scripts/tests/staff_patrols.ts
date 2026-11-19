// SPDX-License-Identifier: GPL-3.0-or-later
//
// A park from the game whose workers were given patrol areas: they keep them once it loads and while the park
// runs. The patrol button of one of them shows their area.
// Screenshot: that worker's area on the map. Give it a park the game saved with patrol areas set:
//
//   ./nob script scripts/tests/staff_patrols.ts -- --no-intro --no-audio --seed 1 --park PARK

export {};

await until(() => park.loaded(), 1200, "the park");
await frames(30);
const box = ui.find("prize_won");
if (box !== undefined) await ui.click(`prize_won/${must(ui.children("prize_won"), "prize_won shown").at(-1)!}`);
const patrolled = () => staff().filter((p) => (p.patrol ?? 0) > 0);
const held = patrolled();
log("patrols", JSON.stringify(held.map((p) => `${p.role}:${p.patrol}`)));
expect(held.length > 0, "workers with patrol areas from the save");

const who = must(
    held.find((p) => p.at !== null && p.state !== undefined),
    "a patrolled worker on the paths",
);
people.show(who.index);
await until(() => ui.find("info") !== undefined && info_tab("infoicon") !== undefined, 120, "the worker's window");
await ui.click(`info/${info_tab("infoicon")}`);
await frames(5);
await ui.click(must(under("info", "Staff_PatrolAreaLarge"), "the patrol button"));
await until(() => under("info", "Rct3pp_PatrolBrush") !== undefined, 60, "the patrol area shown");
const at = must(staff().find((p) => p.index === who.index)?.at, "where the worker stands");
camera.look({ col: at.col, row: at.row }, { col: at.col + 10, row: at.row + 10, up: 30 });
await frames(10);
await screenshot("staff_patrol_area");
await ui.click(must(under("info", "Staff_PatrolAreaLarge"), "the patrol button"));

// They still hold their areas after a minute of park time.
await frames(600);
expect(patrolled().length === held.length, "every patrolled worker still has their area");
