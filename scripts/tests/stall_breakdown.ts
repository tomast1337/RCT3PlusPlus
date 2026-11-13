// SPDX-License-Identifier: GPL-3.0-or-later
//
// A stall that breaks down shuts, the player is told, and its window says so; a mechanic hired and put on the
// paths walks over and mends it, and it sells again.
//
//   ./nob script scripts/tests/stall_breakdown.ts -- --no-audio

export {};

/** The path of the widget named `name` somewhere under `path`. */

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.stalls() !== undefined, 1200, "the tool row and the build lists");
const gate = map.paths().reduce((a, b) => (b.row > a.row ? b : a));
const kind = build.stalls()!.find((s) => s.overlay === "Style/Vanilla/Style" && s.symbol.toLowerCase().includes("burger"));
expect(kind !== undefined, "a plain burger stall");
const col = gate.col + 8;
const row = gate.row - 10;

// A burger stall on open grass, with a short path up to its front.
await look_down(col, row, 40);
expect(build.pick(kind!.overlay, kind!.symbol), `${kind!.symbol} picked`);
pointer.move(...on_view(col, row));
await frames(3);
await pointer.click(...on_view(col, row));
await frames(3);
await ui.click("info/close");
const at = stalls.list().findIndex((s) => s.symbol === `${kind!.symbol}:sta`);
expect(at >= 0, "the stall placed");
const stall = () => stalls.list()[at];
const site = stall().site!;
const kFront: [number, number][] = [
    [0, -1],
    [-1, 0],
    [0, 1],
    [1, 0],
];
const front = kFront[site.direction & 3];
await ui.click("tools/Paths/Obj_Paths");
await until(() => ui.find("footpaths/route") !== undefined, 120, "the path panel");
await ui.toggle("footpaths/follow", false);
const far: [number, number] = [col + 4 * front[0], row + 4 * front[1]];
const by: [number, number] = [col + front[0], row + front[1]];
await ui.toggle("footpaths/route", false);
await pointer.click(...on_view(...far));
await ui.toggle("footpaths/route", true);
for (let wait = 1; wait <= 96 && !has_path(...by); wait *= 2) {
    pointer.move(...on_view(...by));
    await frames(wait);
    await pointer.click(...on_view(...by));
}
await ui.toggle("footpaths/route", false);
await ui.click("footpaths/close");
await until(() => stall().joined === true, 60, "the stall joined by the path at its front");
// Opened, as its window's tab does: a new stall starts shut.
const refused = stalls.setState(at, "open");
expect(!refused, `the stall opened, not refused with ${refused}`);
await until(() => stall().selling === true, 600, "the stall selling");
log("stall", stall().name, JSON.stringify(site));

const told = (key: string) => park.messages().filter((m) => m.key === key).length;
expect(stalls.breakDown(at), "the stall broken down");
await until(() => told("FoodStallBrokenMessage") === 1, 60, "the player told the stall broke down");
expect(stall().broken === true && stall().selling === false, "the stall shut while broken");

// A mechanic hired, put on the far end of its path.
const near = { col: far[0], row: far[1] };
camera.look({ col: col + 0.5, row: row + 0.5 }, { col: col + 0.5 + 8 * front[0] + 4, row: row + 0.5 + 8 * front[1] + 4, up: 12 });
await frames(5);
await ui.click(`tools/Management/${must(ui.children("tools/Management"), "tools/Management shown")[0]}`);
await until(() => ui.find("tools/Management/Man_Staff") !== undefined, 120, "the management menu");
await ui.click("tools/Management/Man_Staff");
await until(() => under("Staff_Title", "Staff_HireMechanicIcon") !== undefined, 120, "the staff window's hire buttons");
const count = staff().length;
await ui.click(must(under("Staff_Title", "Staff_HireMechanicIcon"), "Staff_HireMechanicIcon under Staff_Title"));
await until(() => staff().length === count + 1 && staff()[count].state === "held", 60, "a mechanic hired, in hand");
const drop = map.screen(near.col + 0.5, near.row + 0.5);
expect(drop !== undefined, "the path by the stall in sight");
pointer.move(drop!.x, drop!.y);
await frames(5);
await pointer.click(drop!.x, drop!.y);
await until(() => staff()[count].state === "walking", 600, "the mechanic let go, walking");
await ui.click(under("Staff_Title", "close") ?? under("Staff_Title", "Close") ?? "Staff_Title");

// At work on it: the window shows it broken down.
const mechanic = () => staff()[count];
await until(() => Math.hypot((mechanic().col ?? 1e9) - (site.col + 0.5), (mechanic().row ?? 1e9) - (site.row + 0.5)) < 1.6 || !stall().broken, 600, "the mechanic at the stall");
expect(stall().broken === true, "the stall still broken as the mechanic sets to work");
stalls.show(at);
await frames(30);
await screenshot("stall_breakdown_mending");

await until(() => !stall().broken && stall().selling === true, 3000, "the stall mended and selling again");
await until(() => told("RideFixedMessage") === 1, 60, "the player told the stall was fixed");
await frames(10);
await screenshot("stall_breakdown_fixed");
