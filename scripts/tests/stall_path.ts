// SPDX-License-Identifier: GPL-3.0-or-later
//
// A stall serves only from the path on the tile in front of it, the side it faces. On the game's blank
// landscape a stall is placed on open grass, away from every path. Then:
//
//   - a path laid up to its side does not join it;
//   - a path laid up to its front does, at once.
//
//   ./nob script scripts/tests/stall_path.ts -- --no-audio
//
//          f           S stall, facing f
//          f           s path at its side, f path at its front
//          S s s

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.stalls() !== undefined, 1200, "the tool row and the build lists");
const gate = map.paths().reduce((a, b) => (b.row > a.row ? b : a));
// A food stall: the park runs only stalls that sell food or drink.
const kind = build.stalls()!.find((s) => s.overlay === "Style/Vanilla/Style" && s.symbol.toLowerCase().includes("burger"));
expect(kind !== undefined, "a plain burger stall");
const col = gate.col + 8;
const row = gate.row - 10;

async function look(c: number, r: number, up = 30): Promise<void> {
    camera.look({ col: c + 0.5, row: r + 0.5 }, { col: c + 0.5, row: r + 0.5 + up / 3, up });
    await frames(3);
}
function onView(c: number, r: number): [number, number] {
    const at = map.screen(c, r);
    if (!at)
        throw new Error(`tile ${c}, ${r} is not in sight`);
    return [at.x, at.y];
}

await look(col, row, 40);
expect(build.pick(kind!.overlay, kind!.symbol), `${kind!.symbol} picked`);
pointer.move(...onView(col, row));
await frames(3);
expect(build.placing()?.problem === "none", `the stall placeable on the grass, not ${JSON.stringify(build.placing())}`);
await pointer.click(...onView(col, row));
await frames(3);
await ui.click("info/close");
const at = stalls.list().findIndex((s) => s.symbol === `${kind!.symbol}:sta`);
const stall = () => stalls.list()[at];
const site = stall().site!;
log("stall", kind!.symbol, JSON.stringify(site));
expect(site.col === col && site.row === row, `the stall where it was put, not ${JSON.stringify(site)}`);
expect(!stall().joined, "a stall with no path not run by the park");

// Facings 0 to 3 put a stall's front towards -row, -col, +row and +col.
const kFront: [number, number][] = [
    [0, -1],
    [-1, 0],
    [0, 1],
    [1, 0],
];
const front = kFront[site.direction & 3];
const side = kFront[(site.direction + 1) & 3];

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
const has = (c: number, r: number) => map.paths().some((p) => p.col === c && p.row === r);
async function route(a: [number, number], b: [number, number]): Promise<void> {
    await look((a[0] + b[0]) >> 1, (a[1] + b[1]) >> 1, 40);
    await ui.toggle("footpaths/route", false);
    await pointer.click(...onView(...a));
    await ui.toggle("footpaths/route", true);
    for (let wait = 1; wait <= 96 && !has(...b); wait *= 2) {
        pointer.move(...onView(...b));
        await frames(wait);
        await pointer.click(...onView(...b));
    }
    await ui.toggle("footpaths/route", false);
    expect(has(...a) && has(...b), `a path from ${a} to ${b}, laid ${JSON.stringify(map.paths().filter((p) => Math.abs(p.col - a[0]) + Math.abs(p.row - a[1]) < 8).map((p) => [p.col, p.row]))}`);
}

// At its side, leading away from it: not joined.
await route([col + 3 * side[0], row + 3 * side[1]], [col + side[0], row + side[1]]);
await frames(60);
expect(!stall().joined, "a path at the stall's side not taken");
await screenshot("stall_path_side");

// At its front: joined at once.
await route([col + 3 * front[0], row + 3 * front[1]], [col + front[0], row + front[1]]);
await until(() => stall().joined === true, 60, "the stall joined by the path at its front");
await ui.click("footpaths/close");
// Seen from in front, once the costs have floated away.
camera.look({ col: col + 0.5, row: row + 0.5 }, { col: col + 0.5 + 8 * front[0] + 4 * side[0], row: row + 0.5 + 8 * front[1] + 4 * side[1], up: 12 });
await frames(240);
await screenshot("stall_path_front");
log("stall joined from", JSON.stringify([col + front[0], row + front[1]]));
