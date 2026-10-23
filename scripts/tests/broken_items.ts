// SPDX-License-Identifier: GPL-3.0-or-later
//
// Vandalised path items, and the two ways back: three benches on the road in from the gate of the game's blank
// landscape are broken at once. The park window's replace button puts them all back new, at their full price;
// broken again, a handyman mends the one where they start and goes on to the next, for nothing.
//
//   ./nob script scripts/tests/broken_items.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

/** The path of the widget named `name` somewhere under `path`. */
function under(path: string, name: string): string | undefined {
    for (const child of ui.children(path) ?? []) {
        const at = `${path}/${child}`;
        if (child === name)
            return at;
        const found = under(at, name);
        if (found !== undefined)
            return found;
    }
    return undefined;
}

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "the build lists");
const paths = map.paths();
const gate = paths.reduce((a, b) => (b.row > a.row ? b : a));
const road = paths.filter((p) => p.col === gate.col && p.row < gate.row - 1).sort((a, b) => b.row - a.row).slice(0, 3);
expect(road.length === 3, `3 tiles of road, not ${road.length}`);
camera.look({ col: gate.col + 0.5, row: gate.row - 3.5 }, { col: gate.col + 3.5, row: gate.row + 1.5, up: 6 });
await frames(3);

expect(build.pick("Style/Vanilla/Style", "Bench"), "the bench in the scenery lists");
for (const tile of road) {
    const at = map.screen(tile.col, tile.row);
    expect(at !== undefined, `tile ${tile.col},${tile.row} in sight`);
    pointer.move(at!.x, at!.y);
    await frames(3);
    await pointer.click(at!.x, at!.y);
}
build.stop();
const benches = () => park.pathItems().filter((i) => i.kind === "bench");
const broken = () => benches().filter((i) => i.broken).length;
expect(benches().length === 3, `3 benches, not ${benches().length}`);

expect(park.breakPathItems() === 3, "3 benches broken");
await frames(120);
await screenshot("broken_benches");

// Replaced at once, at a price.
// The blank landscape has no money; in a park with money the replacing is paid for.
const before = park.cash();
// The tool's own button comes first in its group, before its menu's items.
await ui.click(`tools/Management/${ui.children("tools/Management")![0]}`);
await until(() => ui.find("tools/Management/Man_Park") !== undefined, 120, "the management menu");
await ui.click("tools/Management/Man_Park");
await until(() => under("PID_Title", "Staff_JobIsMaintenanceIcon") !== undefined, 120, "the park window's replace button");
const replace = under("PID_Title", "Staff_JobIsMaintenanceIcon")!;
for (let tries = 0; tries < 10 && broken() > 0; ++tries) {
    await ui.click(replace);
    await frames(30);
}
log("cash", before, "->", park.cash());
expect(broken() === 0, "every bench replaced");
expect(before === undefined || park.cash()! < before, "the replacing to be paid for");
await frames(120);
await screenshot("replaced_benches");

// Mended by a handyman, for nothing.
expect(park.breakPathItems() === 3, "3 benches broken again");
const cash = park.cash();
expect(park.addHandyman(road[1].col, road[1].row), "a handyman on the road");
// The park runs at its own pace: a few seconds a bench, and the step to the broken one beside it. Past that they
// wander, so the third is left to chance.
await until(() => broken() <= 1, 20000, "the handyman to mend two benches");
expect(cash === undefined || park.cash()! >= cash, "mending to cost nothing");
quit(0);
