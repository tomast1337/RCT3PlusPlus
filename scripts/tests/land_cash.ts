// SPDX-License-Identifier: GPL-3.0-or-later
//
// Land tools and money, in a park with cash: a dragged brush short of cash raises the ground as far as the cash
// pays for and no further, and painting the ground costs nothing.
//
//   ./nob script scripts/tests/land_cash.ts -- --no-intro --no-audio --no-simulate --seed 1

export {};

async function pick(icon: string): Promise<void> {
    if (!ui.find(`tools/Land/${icon}`)) await ui.click("tools/Land/Land");
    await ui.click(`tools/Land/${icon}`);
}

await park.open("Campaigns/Base/NewBlood.dat");
await park.ready();
await until(() => ui.find("tools/Land/Land") !== undefined, 1200, "the tool row");
await frames(30);
expect(park.cash() !== undefined, "a park with money");

// A flat stretch of the park's own land, seven tiles each way.
const { width, height } = map.size();
const flat = (c: number, r: number): boolean => {
    const t = map.tile(c, r);
    return (
        t !== undefined &&
        t.ownership === "owned" &&
        t.water === undefined &&
        t.corners.every((h) => h === t.corners[0])
    );
};
let site: [number, number] | undefined;
for (let col = 3; col < width - 3 && !site; ++col)
    for (let row = 3; row < height - 3 && !site; ++row) {
        let all = true;
        for (let dc = -3; dc <= 3 && all; ++dc)
            for (let dr = -3; dr <= 3 && all; ++dr)
                all =
                    flat(col + dc, row + dr) &&
                    map.tile(col + dc, row + dr)!.corners[0] === map.tile(col, row)!.corners[0];
        if (all) site = [col, row];
    }
expect(site !== undefined, "a flat stretch of owned land");
const [col, row] = site!;
const top = (): number => Math.max(...map.tile(col, row)!.corners);
pointer.move(0.5, 0.5);
camera.look({ col: col + 0.5, row: row + 0.5 }, { col: col - 3, row: row - 6, up: 40 });
await frames(2);
const at = must(map.screen(col + 0.5, row + 0.5), `tile ${col + 0.5}, ${row + 0.5} in sight`);

// A hill dragged far up with little cash: the cash runs out, the hill stops short.
await pick("Land_Brush_Height");
await ui.click("land_panel/Land_Brush_Height_Mode"); // The brushes open spraying; these strokes are dragged
await until(() => ui.find("land_panel/body/Land_Brush_Height_Mode_Drag") !== undefined, 60, "the mode tab");
await ui.click("land_panel/body/Land_Brush_Height_Mode_Drag");
await ui.click("land_panel/Land_Brush_Height_Raise");
await until(() => ui.find("land_panel/body/Land_Brush_Height_Raise_Hill") !== undefined, 60, "the hill brush");
await ui.click("land_panel/body/Land_Brush_Height_Raise_Hill");
const before = top();
park.setCash(2000);
await pointer.drag([at.x, at.y], [at.x, at.y - 0.3], 30);
await frames(2);
const short = top();
log("short of cash: top", before, "->", short, "cash", park.cash());
await screenshot("short_of_cash");
expect(short > before, "the hill to rise as far as the cash pays for");
expect(park.cash()! >= 0 && park.cash()! <= 1, "the cash spent");

// The same drag with cash to spare goes further.
park.setCash(10000000);
const again = must(map.screen(col + 0.5, row + 0.5, short), `tile ${col + 0.5}, ${row + 0.5} in sight`);
await pointer.drag([again.x, again.y], [again.x, again.y - 0.3], 30);
await frames(2);
log("with cash: top", short, "->", top(), "cash", park.cash());
expect(top() - short > (short - before) * 2, "the hill to rise much further with cash to spare");

// Painting is free: two surfaces, so that one at least differs from the ground's own.
await pick("Land_Brush_Paint");
await until(() => (ui.children("land_panel/body")?.length ?? 0) > 1, 60, "the surfaces");
const cash = park.cash();
const surface = map.tile(col - 3, row + 2)!.surface;
const here = must(map.screen(col - 2.5, row + 2.5), `tile ${col - 2.5}, ${row + 2.5} in sight`);
let painted = false;
for (const name of must(ui.children("land_panel/body"), "land_panel/body shown").slice(0, 2)) {
    await ui.click(`land_panel/body/${name}`);
    await pointer.drag([here.x, here.y], [here.x + 0.02, here.y], 10);
    await frames(2);
    painted = painted || map.tile(col - 3, row + 2)!.surface !== surface;
}
log("painted:", painted, "cash", cash, "->", park.cash());
expect(painted, "the ground painted");
expect(park.cash() === cash, "painting to cost nothing");
