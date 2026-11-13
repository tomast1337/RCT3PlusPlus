// SPDX-License-Identifier: GPL-3.0-or-later
//
// A facility placed in play opens onto every side, as one the park had already does. On the game's blank
// landscape a toilet is placed on open grass, away from every path. Then:
//
//   - a path laid up to its corner does not join it;
//   - a path laid up to a side does, at once, and once open guests use it;
//   - saved and loaded again, it stands where it was, joined to that path.
//
//   ./nob script scripts/tests/facility_path.ts -- --no-audio
//
//      s s T           T toilet, s path at its side
//            c         c path at its corner
//            c c c

export {};

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.facilities() !== undefined, 1200, "the tool row and the build lists");
const gate = map.paths().reduce((a, b) => (b.row > a.row ? b : a));
// The smallest toilet (kind 11).
const kind = build
    .facilities()!
    .filter((f) => f.kind === 11)
    .sort((a, b) => a.width * a.length - b.width * b.length)[0];
expect(kind !== undefined, "a toilet to build");
log("toilet", kind.symbol, kind.overlay, `${kind.width}x${kind.length}`);
const col = gate.col + 8;
const row = gate.row - 10;

await look_down(col, row, 40);
expect(build.pick(kind.overlay, kind.symbol), `${kind.symbol} picked`);
pointer.move(...on_view(col, row));
await frames(3);
expect(build.placing()?.problem === "none", `the toilet placeable on the grass, not ${JSON.stringify(build.placing())}`);
await pointer.click(...on_view(col, row));
await frames(3);
expect(build.placing() === undefined, "the tool put away once the toilet stands");
if (ui.find("info") !== undefined)
    await ui.click("info/close");
const at = rides.list().findIndex((r) => r.symbol === `${kind.symbol}:sat`);
expect(at >= 0, `a ${kind.symbol}:sat among the rides, not ${JSON.stringify(rides.list().map((r) => r.symbol))}`);
const toilet = () => rides.list()[at];
const site = toilet().site!;
log("placed", JSON.stringify(site));
expect(site.col === col && site.row === row, `the toilet where it was put, not ${JSON.stringify(site)}`);
expect(!toilet().joined, "a toilet with no path not run by the park");

await ui.click("tools/Paths/Obj_Paths");
await until(() => ui.find("footpaths/route") !== undefined, 120, "the path panel");
await ui.toggle("footpaths/follow", false);
await ui.click("footpaths/queues");
await frames(2);
const queues = ui.children("footpaths/body") ?? [];
await ui.click("footpaths/paths");
await frames(2);
// The path styles: the buttons on the paths page that the queues page lacks.
const styles = (ui.children("footpaths/body") ?? []).filter((n) => !queues.includes(n));
await ui.click(`footpaths/body/${styles.find((s) => s.toLowerCase().includes("asphalt")) ?? styles[0]}`);
await frames(2);
async function route(a: [number, number], b: [number, number]): Promise<void> {
    await look_down((a[0] + b[0]) >> 1, (a[1] + b[1]) >> 1, 40);
    await ui.toggle("footpaths/route", false);
    await pointer.click(...on_view(...a));
    await ui.toggle("footpaths/route", true);
    for (let wait = 1; wait <= 96 && !has_path(...b); wait *= 2) {
        pointer.move(...on_view(...b));
        await frames(wait);
        await pointer.click(...on_view(...b));
    }
    await ui.toggle("footpaths/route", false);
    expect(has_path(...a) && has_path(...b), `a path from ${a} to ${b}, laid ${JSON.stringify(map.paths().filter((p) => Math.abs(p.col - a[0]) + Math.abs(p.row - a[1]) < 8).map((p) => [p.col, p.row]))}`);
}

// At its corner: not joined.
await route([col + kind.width + 2, row + kind.length], [col + kind.width, row + kind.length]);
await frames(60);
expect(!toilet().joined, `a path at the toilet's corner not taken, not ${JSON.stringify(toilet().servedFrom)}`);

// At its side: joined at once.
await route([col - 3, row], [col - 1, row]);
await until(() => toilet().joined === true, 60, "the toilet joined by the path at its side");
log("served from", JSON.stringify(toilet().servedFrom));
expect(toilet().servedFrom?.col === col - 1 && toilet().servedFrom?.row === row, `it served from the path at its side, not ${JSON.stringify(toilet().servedFrom)}`);
await ui.click("footpaths/close");
await frames(3);
camera.look({ col: col + 0.5, row: row + 0.5 }, { col: col - 6.5, row: row + 8.5, up: 14 });
await frames(240);
await screenshot("facility_path_joined");

// Open, guests dropped on the paths use it.
const why = rides.setState(at, "open");
expect(why == null, `the toilet opened, not kept closed by ${why}`);
park.dropParties(40, 3);
await park.advanceUntil(() => (toilet().customers ?? 0) > 0, 600, "a guest using the toilet");
log("customers", toilet().customers);
// One going in, close up at the door: they step in and are out of sight inside.
const user = () => people.list().find((p) => p.state === "buying" && p.at !== null && p.at.col >= col - 1 && p.at.col <= col + kind.width && p.at.row >= row - 1 && p.at.row <= row + kind.length);
await park.advanceUntil(() => user() !== undefined, 600, "a guest going into the toilet");
park.setTime(0.5); // Shown by day
await frames(20);
camera.look({ col: col + 0.3, row: row + 0.5, up: 0.8 }, { col: col - 1.6, row: row + 1.4, up: 2.2 });
await frames(4);
log("in the toilet, drawn at", JSON.stringify({ col: user()?.col, row: user()?.row }));
await screenshot("facility_path_using");

// Saved and loaded again.
await click_tool("Files");
await ui.click("tools/Files/File_SaveGame");
await until(() => ui.find("park_files") !== undefined, 120, "the save window");
await ui.click("park_files/content/open");
await until(() => ui.find("park_files") === undefined, 120, "the save window to close");
if (!ui.find("tools/Files/File_LoadGame"))
    await click_tool("Files");
await until(() => ui.find("tools/Files/File_LoadGame") !== undefined, 120, "the files menu");
await ui.click("tools/Files/File_LoadGame");
// Loading asks first whether to save; the park was saved just now.
await until(() => ui.find("load_question") !== undefined, 120, "the question before loading");
await ui.click("load_question/content/answer1");
const first = "park_files/content/list/view/content/row0";
await until(() => ui.find(first) !== undefined, 120, "the save in the load window");
await ui.click(first);
await ui.click("park_files/content/open");
await until(() => park.loaded() && rides.list().some((r) => r.symbol === `${kind.symbol}:sat`), 1200, "the saved park");
await frames(10);
const again = must(rides.list().find((r) => r.symbol === `${kind.symbol}:sat`), "the toilet after loading");
log("loaded", JSON.stringify({ site: again.site, joined: again.joined, servedFrom: again.servedFrom, open: again.open }));
expect(again.site?.col === col && again.site?.row === row, `the toilet where it stood, not ${JSON.stringify(again.site)}`);
expect(again.joined && again.servedFrom?.col === col - 1 && again.servedFrom?.row === row, `it joined to the same path, not ${JSON.stringify(again.servedFrom)}`);
park.setTime(0.5); // Shown by day
await look_down(col, row, 30);
await screenshot("facility_path_loaded");

// Pulled down from its window: the park no longer runs it.
const index = rides.list().findIndex((r) => r.symbol === `${kind.symbol}:sat`);
rides.show(index);
await frames(10);
await ui.click("info/body/Attraction_General_Delete");
await frames(2);
await ui.click("demolish_question/content/GUI2MiniDialog_Confirm");
await frames(2);
const gone = rides.list()[index];
expect(gone.demolished && !gone.joined, `the toilet pulled down, not ${JSON.stringify(gone)}`);
