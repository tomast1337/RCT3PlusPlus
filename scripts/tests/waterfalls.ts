// SPDX-License-Identifier: GPL-3.0-or-later
//
// Waterfalls: a scenario's falls drawn from the park, seen from below; one made with the tool, cutting its
// spillway into the bank, and deleted again, putting the ground back.
//
//   ./nob script scripts/tests/waterfalls.ts -- --no-intro --no-audio --no-simulate --seed 1

export {};

await park.open("Campaigns/Base/Soaked/OasisOfFun.dat");
await until(() => ui.find("tools/Land/Land") !== undefined, 300, "the tool row");

const falls = map.waterfalls();
log("waterfalls", JSON.stringify(falls));
expect(falls.length === 6, "the scenario's six waterfalls");
for (const f of falls)
    expect(f.top > f.bottom && f.width > 0, "each to fall from a higher lake to a lower one");

// The tallest, from in front of where it lands and a little to the side.
const tall = falls.reduce((a, b) => (a.top - a.bottom >= b.top - b.bottom ? a : b));
const out = [tall.landCol - tall.col, tall.landRow - tall.row];
const span = Math.hypot(out[0], out[1]);
const dir = [out[0] / span, out[1] / span];
camera.look({ col: (tall.col + tall.landCol) / 2, row: (tall.row + tall.landRow) / 2, up: (tall.top - tall.bottom) / 2 }, { col: tall.landCol + dir[0] * 14 + dir[1] * 6, row: tall.landRow + dir[1] * 14 - dir[0] * 6, up: (tall.top - tall.bottom) * 0.6 });
await frames(10);
await screenshot("tallest");
await frames(30);
await screenshot("tallest_later");

// The waterfall tool: the same two lakes clicked again make a second fall over the same edge; one lake
// clicked twice makes none.
const first = falls[0];
const upper = { col: Math.floor(first.col - 0.5 * Math.sign(first.landCol - first.col)), row: Math.floor(first.row - 0.5 * Math.sign(first.landRow - first.row)) };
const lower = { col: Math.floor(first.landCol), row: Math.floor(first.landRow) };
log("lakes", JSON.stringify(map.tile(upper.col, upper.row)), JSON.stringify(map.tile(lower.col, lower.row)));
expect(Math.abs((map.tile(upper.col, upper.row)?.water ?? -1e9) - first.top) < 0.1 && map.tile(lower.col, lower.row)?.water !== undefined, "the first fall's lakes either side of it");
camera.look({ col: (upper.col + lower.col) / 2 + 0.5, row: (upper.row + lower.row) / 2 + 0.5 }, { col: (upper.col + lower.col) / 2 - 2, row: (upper.row + lower.row) / 2 - 10, up: 120 });
await frames(2);
if (!ui.find("tools/Land/Land_Waterfall"))
    await ui.click("tools/Land/Land");
await ui.click("tools/Land/Land_Waterfall");
function click_tile(col: number, row: number): Promise<void> {
    const at = map.screen(col, row);
    if (!at)
        throw new Error(`tile ${col}, ${row} is not in sight`);
    return pointer.click(at.x, at.y);
}
await click_tile(upper.col, upper.row);
await click_tile(upper.col, upper.row);
await frames(2);
expect(map.waterfalls().length === falls.length, "no fall from a lake into itself");
// The ground between the lakes, to see the spillway cut and put back.
const ground = (): string => {
    const out: number[] = [];
    for (let r = Math.min(upper.row, lower.row) - 6; r <= Math.max(upper.row, lower.row) + 6; ++r)
        for (let c = Math.min(upper.col, lower.col) - 6; c <= Math.max(upper.col, lower.col) + 6; ++c)
            out.push(...(map.tile(c, r)?.corners ?? []));
    return JSON.stringify(out);
};
const before = ground();
await click_tile(lower.col, lower.row); // Picked first this time; the fall still pours from the higher lake
await click_tile(upper.col, upper.row);
await frames(2);
const made = map.waterfalls();
log("made", JSON.stringify(made[made.length - 1]));
expect(made.length === falls.length + 1, "a new waterfall");
expect(Math.abs(made[made.length - 1].top - first.top) < 0.1 && Math.abs(made[made.length - 1].bottom - first.bottom) < 0.1, "it to join the same two lakes");
await screenshot("made");

// Unless the lower lake lies against the edge, the new fall cut a spillway into the bank, one tile out; deleting
// it with a right click on its water puts the ground back.
const fresh = made[made.length - 1];
const reach = Math.abs(lower.col - upper.col) + Math.abs(lower.row - upper.row);
log("reach", reach, "cut", ground() !== before);
expect(reach <= 1 || ground() !== before, "the spillway cut");
const middle = map.screen((fresh.col + fresh.landCol) / 2, (fresh.row + fresh.landRow) / 2);
if (!middle)
    throw new Error("the new fall is not in sight");
await pointer.click(middle.x, middle.y, "right");
await frames(2);
log("falls after deleting", map.waterfalls().length);
expect(map.waterfalls().length === falls.length, "the new fall deleted");
expect(ground() === before, "the ground put back");
await screenshot("deleted");

// The water tool raising a lake that has waterfalls asks first, and pours nothing: no keeps the falls, yes takes
// those of that lake away, for the player to pour again.
if (!ui.find("tools/Land/Land_Water"))
    await ui.click("tools/Land/Land");
await ui.click("tools/Land/Land_Water");
await frames(2);
// Bank tiles beside a lake with a waterfall that rise a little over it, clicked until water poured from one reaches the lake.
const levels = falls.flatMap((f) => [f.top, f.bottom]);
const size = map.size();
const banks: { col: number; row: number }[] = [];
for (let r = 1; r < size.height - 1; ++r)
    for (let c = 1; c < size.width - 1; ++c) {
        const t = map.tile(c, r);
        if (!t || !t.owned)
            continue;
        const beside = [t, map.tile(c - 1, r), map.tile(c + 1, r), map.tile(c, r - 1), map.tile(c, r + 1)].find((n) => n?.water !== undefined && levels.some((l) => Math.abs(l - (n.water ?? 1e9)) < 0.1))?.water;
        const middle = t.corners.reduce((x, y) => x + y, 0) / 4;
        if (beside !== undefined && middle > beside + 0.3 && middle < beside + 0.9 && Math.min(...t.corners) < beside)
            banks.push({ col: c, row: r });
    }
log("banks", banks.length);
let bank: { x: number; y: number } | undefined;
for (const b of banks.slice(0, 60)) {
    camera.look({ col: b.col + 0.5, row: b.row + 0.5 }, { col: b.col - 1.5, row: b.row - 8, up: 140 });
    await frames(2);
    const at = map.screen(b.col, b.row);
    if (!at)
        continue;
    await pointer.click(at.x, at.y);
    await frames(2);
    if (ui.find("lake_question"))
        await ui.click("lake_question/content/GUI2MiniDialog_Cancel");
    if (ui.find("flood_question")) {
        log("bank", JSON.stringify(b), JSON.stringify(map.tile(b.col, b.row)));
        bank = at;
        break;
    }
}
const lakes = map.water().length;
expect(bank !== undefined, "a question before a lake with waterfalls is raised");
await screenshot("flood_question");
await ui.click("flood_question/content/GUI2MiniDialog_Cancel");
await frames(2);
expect(map.water().length === lakes && map.waterfalls().length === falls.length, "the lake and its falls kept on a no");
await pointer.click(bank!.x, bank!.y);
await frames(2);
await ui.click("flood_question/content/GUI2MiniDialog_Confirm");
await frames(3);
const kept = map.waterfalls();
log("falls after the flood question", falls.length, "->", kept.length, "lakes", lakes, "->", map.water().length);
expect(kept.length > 0 && kept.length < falls.length, "the raised lake's falls gone on a yes");
expect(map.water().length === lakes, "no water poured by the answer");
await screenshot("flood_falls_gone");

// The water tool on a lake that has waterfalls asks first; yes takes the lake and its falls away.
const land = { col: Math.floor(kept[0].landCol), row: Math.floor(kept[0].landRow) };
camera.look({ col: land.col + 0.5, row: land.row + 0.5 }, { col: land.col - 2, row: land.row - 10, up: 120 });
await frames(2);
await click_tile(land.col, land.row);
await frames(2);
expect(ui.find("lake_question") !== undefined, "a question before a lake with waterfalls goes");
await screenshot("lake_question");
await ui.click("lake_question/content/GUI2MiniDialog_Cancel");
await frames(2);
expect(map.water().length === lakes && map.waterfalls().length === kept.length, "the lake and its falls kept on a no");
await click_tile(land.col, land.row);
await frames(2);
await ui.click("lake_question/content/GUI2MiniDialog_Confirm");
await frames(3);
log("lakes", lakes, "->", map.water().length, "falls", kept.length, "->", map.waterfalls().length);
expect(map.water().length === lakes - 1 && map.waterfalls().length < kept.length, "the lake and its falls gone on a yes");
await screenshot("lake_removed");
