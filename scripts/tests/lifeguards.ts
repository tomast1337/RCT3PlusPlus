// SPDX-License-Identifier: GPL-3.0-or-later
//
// A park from the game whose pool has a lifeguard chair: its lifeguard sits in the chair, unpaid, and the staff
// window does not list them (they come and go with the chair, nobody hires them). Give it a park the game saved
// with a lifeguard chair by a pool:
// Screenshots: the lifeguard in their chair, close from the side facing out of it, and the staff window without them.
//
//   ./nob script scripts/tests/lifeguards.ts -- --no-intro --no-audio --seed 1 --park PARK

export {};

/** The path of the widget named `name` somewhere under `path`. */

await until(() => park.loaded(), 1200, "the park");
await frames(30);
const chairs = map.objects().filter((o) => o.symbol.toLowerCase().includes("lifeguard"));
log("chairs", chairs.map((c) => `${c.symbol} ${c.col},${c.row}`).join(" "));
expect(chairs.length > 0, "a lifeguard chair in the park");
const guards = () => people.list().filter((p) => p.staff && p.role === "other");
await until(() => guards().length === chairs.length, 300, "a lifeguard for each chair");
log("lifeguards", guards().map((g) => `${g.col},${g.row}`).join(" "));
for (const c of chairs)
    expect(guards().some((g) => Math.floor(g.col ?? -1) === c.col && Math.floor(g.row ?? -1) === c.row), `a lifeguard sitting in the chair at ${c.col},${c.row}`);
const guard = guards()[0];
camera.look({ col: guard.col!, row: guard.row! }, { col: guard.col! + 2.5, row: guard.row! + 3, up: (guard.up ?? 0) + 4 });
await frames(30);
await screenshot("lifeguard_in_chair");
// Close, from the side: the lifeguard sits back in the seat, looking out over the front of the chair.
camera.look({ col: guard.col!, row: guard.row!, up: (guard.up ?? 0) + 1.5 }, { col: guard.col! + 2, row: guard.row! + 0.2, up: (guard.up ?? 0) + 2.5 });
await frames(30);
await screenshot("lifeguard_facing");

// The staff window: a row for every other worker, none for the lifeguards.
await ui.click(`tools/Management/${must(ui.children("tools/Management"), "tools/Management shown")[0]}`);
await until(() => ui.find("tools/Management/Man_Staff") !== undefined, 120, "the management menu");
await ui.click("tools/Management/Man_Staff");
await until(() => under("Staff_Title", "table") !== undefined, 120, "the staff window");
await frames(10);
const list = `${must(under("Staff_Title", "table"), "table under Staff_Title")}/view/content`;
const rows = ui.children(list) ?? [];
// The list starts with the vendors' switch off.
const listed = people.list().filter((p) => p.staff && p.role !== "other" && p.role !== "vendor").length;
log("rows", rows.length, "workers listed", listed, "lifeguards", guards().length);
expect(rows.length === listed, `${listed} rows, the lifeguards left out, not ${rows.length}`);
await screenshot("lifeguard_staff_list");
