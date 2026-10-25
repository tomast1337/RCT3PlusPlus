// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pulling attractions down in the game's Vanilla Hills. A flat ride with guests queueing or riding is deleted
// from its window: the question asks first, and No keeps it; Yes takes it away, its guests walk on and the park
// gets money back. A stall goes the same way.
//
//   ./nob script scripts/tests/demolish.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

await park.open("Campaigns/Base/VanillaHills.dat");
await park.ready();
await frames(60);
const busy = () => rides.list().findIndex((r) => r.flat && !r.demolished && r.joined && ((r.queue ?? 0) > 0 || (r.riders ?? 0) > 0));
await until(() => busy() >= 0, 3000, "a flat ride with guests on it or queueing");
const ride = busy();
const before = rides.list()[ride];
log("pulling down", JSON.stringify({ name: before.name, queue: before.queue, riders: before.riders }));
const guests = park.guests();

/** Asks to delete what the info panel shows, and answers with `answer`. */
async function answer(answer: "GUI2MiniDialog_Confirm" | "GUI2MiniDialog_Cancel", shot?: string): Promise<void> {
    await ui.click("info/body/Attraction_General_Delete");
    await frames(2);
    expect(ui.find("demolish_question") !== undefined, "the question asked");
    if (shot)
        await screenshot(shot);
    await ui.click(`demolish_question/content/${answer}`);
    await frames(2);
    expect(ui.find("demolish_question") === undefined, "the question gone once answered");
}

rides.show(ride);
await frames(10);
await answer("GUI2MiniDialog_Cancel", "demolish_question");
expect(!rides.list()[ride].demolished, "No keeping the ride");
const cash = park.cash() ?? 0;
await answer("GUI2MiniDialog_Confirm");
const after = rides.list()[ride];
expect(after.demolished && !after.joined, `the ride pulled down (${JSON.stringify(after)})`);
expect((park.cash() ?? 0) > cash, `money back (${cash} then ${park.cash()})`);
expect(ui.find("info") === undefined || rides.shown()?.ride !== ride, "its window gone");
expect(park.guests() === guests, `its guests still in the park (${guests} then ${park.guests()})`);
await frames(300);
expect(park.guests() >= guests - 5, "the guests walking on, not lost");
await screenshot("demolish_ride");

// A stall placed beside a path and pulled down before it ever opened gives all its price back.
await until(() => build.stalls() !== undefined, 1200, "the build lists");
const kind = build.stalls()!.find((s) => s.overlay === "Style/Vanilla/Style" && s.goods.length > 0)!;
expect(build.pick(kind.overlay, kind.symbol), `${kind.symbol} picked`);
let spot: { x: number; y: number } | undefined;
for (const p of map.paths().slice(0, 40)) {
    camera.look({ col: p.col + 0.5, row: p.row + 0.5 }, { col: p.col + 0.5, row: p.row + 10.5, up: 25 });
    await frames(2);
    for (const [dc, dr] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
        const at = map.screen(p.col + dc, p.row + dr);
        if (at === undefined)
            continue;
        pointer.move(at.x, at.y);
        await frames(3);
        if (build.placing()?.problem === "none") {
            spot = at;
            break;
        }
    }
    if (spot)
        break;
}
expect(spot !== undefined, "somewhere beside a path to place the stall");
await pointer.click(spot!.x, spot!.y);
await frames(3);
const stall = stalls.list().findIndex((s) => s.symbol === `${kind.symbol}:sta`);
expect(stall >= 0 && stalls.list()[stall].shown, "the stall placed, its window open");
// The park's takings and costs move the cash a little between frames.
const paid = park.cash() ?? 0;
await answer("GUI2MiniDialog_Confirm");
expect(stalls.list()[stall].demolished, "the stall pulled down");
const back = (park.cash() ?? 0) - paid;
expect(Math.abs(back - kind.price) < 100, `all of its price ${kind.price} back, never opened, not ${back}`);
await screenshot("demolish_stall");
await frames(60);
quit(0);
