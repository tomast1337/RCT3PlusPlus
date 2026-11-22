// SPDX-License-Identifier: GPL-3.0-or-later
//
// A raised ride entrance over a path on the ground. On the game's blank landscape the dinghy slide's saved design
// "Splash Down" is laid; its entrance stands raised beside its station, on a floor of its own. A path is laid on the
// ground along the entrance's row, passing under it. Then:
//
//   - the entrance keeps its raised floor, and the path runs on under it;
//   - with its fence and entrance changed to another theme's from the ride's window, it still has its floor.
//
// A look from the side, along the path, shows both.
//
//   ./nob script scripts/tests/entrance_over_path.ts -- --no-audio --no-intro

export {};

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "tools");
expect(build.pick("tracks/coasters/Dingy/Dingy", "Dingy", "designs"), "a dinghy slide to build");
await until(() => ui.find("design_browser") !== undefined, 600, "the designs window");
await frames(3);
const row = build.construction()!.designs.findIndex((d) => d.includes("Splash Down"));
expect(row >= 0, "Splash Down among the designs");
const list = must(ui.find("design_browser/content/list"), "design_browser/content/list shown");
const button = must(ui.find("design_browser/content/build"), "design_browser/content/build shown");
const unit = (button.bottom - button.top) / 22;
// Under the column heads and the custom track's row, which stays first.
await pointer.click(list.x, list.top + (18 + 16 * (row + 1) + 8) * unit);
await frames(5);
await ui.click("design_browser/content/build");
await frames(10);
let here: [number, number] = [0, 0];
let clear = false;
for (let k = 0; k < 60 && !clear; ++k) {
    here = [0.05 + 0.06 * (k % 15), 0.3 + 0.1 * Math.floor(k / 15)];
    pointer.move(...here);
    await frames(3);
    const c = build.construction();
    clear = c !== undefined && c.ghost && c.refusal === "";
}
expect(clear, "open ground for the design");
await pointer.click(...here);
await frames(30);
const ride = must(rides.list().at(-1), "the ride laid").index;
const now = () => rides.list()[ride];
const door = must(now().entrance, "the design's entrance");
const floor = () => map.pavings().find((p) => p.col === door.col && p.row === door.row);
const raised = must(floor(), "the entrance's floor").height;
log("entrance", JSON.stringify(door), now().entranceItem, "floor at", raised);
expect(raised > 0, "the entrance raised off the ground");

// A path on the ground along the entrance's row, under it.
await ui.click("info/close");
await ui.click("tools/Paths/Obj_Paths");
await until(() => ui.find("footpaths/route") !== undefined, 120, "the path panel");
await ui.toggle("footpaths/follow", false);
await ui.toggle("footpaths/route", false);
await ui.click("footpaths/paths");
await frames(2);
const styles = ui.children("footpaths/body") ?? [];
await ui.click(`footpaths/body/${styles.find((n) => /asphalt/i.test(n)) ?? styles[0]}`);
await frames(2);
const ground = (c: number) => map.paths().some((p) => p.col === c && p.row === door.row && !p.raised);
await look_down(door.col, door.row, 25);
await pointer.click(...on_view(door.col - 3, door.row));
await ui.toggle("footpaths/route", true);
for (let wait = 1; wait <= 96 && !ground(door.col + 3); wait *= 2) {
    pointer.move(...on_view(door.col + 3, door.row));
    await frames(wait);
    await pointer.click(...on_view(door.col + 3, door.row));
}
await ui.toggle("footpaths/route", false);
await ui.click("footpaths/close");
expect(ground(door.col), "a path on the ground under the entrance");

/** The entrance's raised floor is laid, with a deck at its height. */
const floored = () => {
    const f = floor();
    const deck = map
        .objects()
        .some((o) => o.col === door.col && o.row === door.row && o.height === raised && o.overlay.startsWith("Path/"));
    log("floor", JSON.stringify(f), "deck", deck);
    return f !== undefined && f.style !== "" && f.height === raised && deck;
};
const look = async (name: string) => {
    pointer.move(0.5, 0.95);
    camera.look({ col: door.col + 0.5, row: door.row + 0.5 }, { col: door.col - 3.5, row: door.row + 0.5, up: 1.5 });
    await frames(10);
    await screenshot(name);
};
expect(floored(), "the entrance's raised floor kept over the path");
expect(ground(door.col), "the path under it kept");
await look("entrance_over_path");

// Another theme's fence and entrance, picked from the ride's window.
/** Picks rows of the list `name` from the top down until `has` gives `want`. */
async function pickRow(name: string, has: () => string, want: string): Promise<void> {
    const body = must(ui.find("info/body"), "the window's body");
    for (let k = 0; k < 12 && has() !== want; ++k) {
        const list = must(ui.find(`info/body/${name}`), name);
        pointer.move(body.x, body.y);
        pointer.wheel(list.bottom > body.bottom ? -3 : list.top < body.top ? 3 : 0);
        await frames(3);
        await ui.click(`info/body/${name}`);
        await frames(3);
        keys.press("Home");
        for (let i = 0; i < k; ++i) keys.press("Down");
        await frames(1);
        keys.press("Enter");
        await frames(5);
    }
    expect(has() === want, `${name} picked as ${want}, not ${has()}`);
}
rides.show(ride);
await until(() => ui.find("info/body/Attraction_General_PickFence") !== undefined, 120, "the ride's window");
await ui.click("info/body/Attraction_General_PickFence");
await until(() => ui.find("info/body/fence_style") !== undefined, 60, "the looks to pick from");
await pickRow("fence_style", () => now().fence, "SteelWall_FenceStraight");
await pickRow("entrance_style", () => now().entranceItem, "RocketEntrance");
await ui.click("info/close");
expect(floored(), "the entrance's raised floor kept with its new look");
expect(ground(door.col), "the path under it still there");
await look("restyled_entrance_over_path");
quit(0);
