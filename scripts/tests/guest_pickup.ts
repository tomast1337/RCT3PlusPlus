// SPDX-License-Identifier: GPL-3.0-or-later
//
// Picking a guest up and putting them down. Their window's pick-up button hands them to the pointer, where
// they hang over what it is on; a click on a path lets them go there, to fall and walk on; a click on the
// park's bare land lets them go too, and they walk to the nearest path; water does not take them; a right
// click puts them back. Their Cash tab shows what they have and have spent.
//
//   ./nob script scripts/tests/guest_pickup.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

await park.open("Campaigns/Base/ChequeredFlag.dat");
await park.ready();
await frames(30);
const cash = park.cash();
const dropped = park.dropParties(6, 3);
expect(dropped.length > 0, "parties dropped in the park");
const paths = map.paths().filter((p) => !p.queue && !p.raised);
const onPath = new Set(map.paths().map((p) => `${p.col},${p.row}`));
/** Whether tile (col, row) is in sight away from the window and the bars. */
const clear = (col: number, row: number) => {
    const at = map.screen(col, row);
    return at !== undefined && at.x > 0.05 && at.x < 0.95 && at.y > 0.1 && at.y < 0.88 && !(at.x > 0.55 && at.y > 0.65);
};
const tileOf = (p: { col?: number; row?: number }) => `${Math.floor(p.col!)},${Math.floor(p.row!)}`;

// A guest walking the paths, shown in the park.
await until(() => people.list().some((p) => !p.staff && p.state === "walking"), 600, "a guest walking");
const who = people.list().findIndex((p) => !p.staff && p.state === "walking");
const state = () => people.list()[who].state;
const guest = people.list()[who];
camera.look({ col: guest.col!, row: guest.row! }, { col: guest.col! + 5, row: guest.row! + 5, up: 25 });
await frames(10);
people.show(who);
await until(() => ui.find("info") !== undefined, 120, "the guest's window");
const tab = (part: string) => ui.children("info")!.find((t) => t.toLowerCase().includes(part));
const cashTab = tab("cash");
if (cashTab !== undefined) {
    await ui.click(`info/${cashTab}`);
    await frames(10);
    await screenshot("cash_tab");
    await ui.click(`info/${tab("options")}`);
    await frames(5);
}

/** Finds the pick-up button wherever the window holds it. */
const button = (path: string): string | undefined => {
    for (const child of ui.children(path) ?? []) {
        const at = `${path}/${child}`;
        if (child === "PickUp")
            return at;
        const found = button(at);
        if (found !== undefined)
            return found;
    }
    return undefined;
};

const pickUp = async () => {
    const at = button("info");
    expect(at !== undefined, "a pick-up button on the guest's window");
    await ui.click(at!);
    await until(() => state() === "held", 60, "the guest to be held");
};

/** Hangs the held guest over tile (col, row) and clicks there. */
const dropAt = async (col: number, row: number) => {
    const at = map.screen(col, row);
    expect(at !== undefined, `tile ${col},${row} in sight`);
    pointer.move(at!.x, at!.y);
    await frames(5);
    await pointer.click(at!.x, at!.y);
    await frames(2);
};

// Onto a path a few tiles off: they fall, then walk on from there.
await pickUp();
const target = paths.find((p) => Math.abs(p.col - guest.col!) + Math.abs(p.row - guest.row!) >= 3 && clear(p.col, p.row));
expect(target !== undefined, "a path in sight a few tiles off");
const over = map.screen(target!.col, target!.row)!;
pointer.move(over.x, over.y);
await frames(10);
const hung = people.list()[who];
expect(tileOf(hung) === `${target!.col},${target!.row}`, "the guest hanging over the tile under the pointer");
await screenshot("held");
// A close look at them hanging, the pointer kept over the same spot.
camera.look({ col: hung.col!, row: hung.row!, up: hung.up! - 2 }, { col: hung.col! + 1.5, row: hung.row! + 1.5, up: hung.up! + 2 });
await frames(5);
await screenshot("held_close");
camera.look({ col: guest.col!, row: guest.row! }, { col: guest.col! + 5, row: guest.row! + 5, up: 25 });
pointer.move(over.x, over.y);
await frames(5);
await pointer.click(over.x, over.y);
await until(() => state() !== "held", 30, "the guest let go");
await frames(3);
await screenshot("falling");
await until(() => state() === "walking", 300, "the guest walking again");
expect(tileOf(people.list()[who]) === `${target!.col},${target!.row}`, "the guest walking on from where they were put");

// A right click puts them back.
const before = tileOf(people.list()[who]);
await pickUp();
await dropAt(target!.col + 0.5, target!.row + 0.5);
await until(() => state() !== "held", 30, "let go");
await until(() => state() === "walking", 300, "walking");
await pickUp();
const away = map.screen(target!.col, target!.row)!;
pointer.move(away.x, away.y);
await frames(5);
await pointer.click(away.x, away.y, "right");
await until(() => state() === "walking", 300, "the guest put back and walking");
log("put back", before, tileOf(people.list()[who]));

// Bare land of the park near a path: they walk to the nearest path.
const size = map.size();
let bare: { col: number; row: number } | undefined;
for (let d = 2; d < 8 && bare === undefined; ++d)
    for (const p of paths) {
        const t = map.tile(p.col + d, p.row);
        if (t !== undefined && t.owned && t.water === undefined && !onPath.has(`${p.col + d},${p.row}`) && p.col + d < size.width && clear(p.col + d, p.row)) {
            bare = { col: p.col + d, row: p.row };
            break;
        }
    }
if (bare !== undefined) {
    await pickUp();
    await dropAt(bare.col, bare.row);
    await until(() => state() !== "held", 30, "the guest let go over bare land");
    await screenshot("bare_land");
    await until(() => state() === "walking", 3000, `the guest at a path, walking (${JSON.stringify(people.list()[who])})`);
    expect(onPath.has(tileOf(people.list()[who])), `the guest walking on a path after landing on bare land, not at ${tileOf(people.list()[who])}`);
}
else
    log("no bare land of the park in sight near a path");

// Water does not take them.
let wet: { col: number; row: number } | undefined;
for (let c = 0; c < size.width && wet === undefined; ++c)
    for (let r = 0; r < size.height; ++r) {
        const t = map.tile(c, r);
        if (t !== undefined && t.owned && t.water !== undefined && t.water > Math.max(...t.corners) && clear(c, r)) {
            wet = { col: c, row: r };
            break;
        }
    }
if (wet !== undefined) {
    await pickUp();
    await dropAt(wet.col, wet.row);
    await frames(10);
    expect(state() === "held", "the guest still held over water");
    await screenshot("over_water");
    await pointer.click(0.5, 0.5, "right");
    await until(() => state() === "walking", 300, "the guest put back from over the water");
}
else
    log("no water in sight");

log("cash", cash, "to", park.cash());
quit(0);
