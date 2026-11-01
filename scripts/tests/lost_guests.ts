// SPDX-License-Identifier: GPL-3.0-or-later
//
// A party member picked up and put down far from the rest stays in the party: the others wait for them, and they
// walk back to them, then all go on together.
//
//   ./nob script scripts/tests/lost_guests.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

await park.open("Campaigns/Base/ChequeredFlag.dat");
await park.ready();
await frames(30);
expect(park.dropParties(6, 3).length > 0, "parties dropped in the park");
const paths = map.paths().filter((p) => !p.queue && !p.raised);
/** Whether tile (col, row) is in sight away from the window and the bars. */
const clear = (col: number, row: number) => {
    const at = map.screen(col, row);
    return at !== undefined && at.x > 0.05 && at.x < 0.95 && at.y > 0.1 && at.y < 0.88 && !(at.x > 0.55 && at.y > 0.65);
};

// One of a party of guests walking the paths.
const inParty = () => people.list().findIndex((p) => !p.staff && p.state === "walking" && p.party !== undefined && people.list().filter((q) => q.party === p.party).length > 1);
await park.advanceUntil(() => inParty() >= 0, 10, "a party walking");
const who = inParty();
const party = people.list()[who].party!;
const me = () => people.list()[who];
const others = () => people.list().filter((p, i) => i !== who && p.party === party);
const guest = me();
camera.look({ col: guest.col!, row: guest.row! }, { col: guest.col! + 8, row: guest.row! + 8, up: 35 });
await frames(10);
people.show(who);
await until(() => ui.find("info") !== undefined, 120, "the guest's window");
const tab = (part: string) => ui.children("info")!.find((t) => t.toLowerCase().includes(part));
const options = tab("options");
if (options !== undefined) {
    await ui.click(`info/${options}`);
    await frames(5);
}
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
const pick = button("info");
expect(pick !== undefined, "a pick-up button on the guest's window");
await ui.click(pick!);
await until(() => me().state === "held", 60, "the guest held");

// Put down on a path well away from the rest, in sight.
const far = paths
    .filter((p) => clear(p.col, p.row))
    .sort((p, q) => Math.hypot(q.col - guest.col!, q.row - guest.row!) - Math.hypot(p.col - guest.col!, p.row - guest.row!))[0];
expect(far !== undefined && Math.hypot(far.col - guest.col!, far.row - guest.row!) >= 5, "a path in sight well away");
const over = map.screen(far!.col, far!.row)!;
pointer.move(over.x, over.y);
await frames(10);
await pointer.click(over.x, over.y);
await until(() => me().state !== "held", 30, "the guest let go");
await park.advanceUntil(() => me().party === party, 10, `the guest back in their party (${JSON.stringify(me())})`);
const apart = Math.max(...others().map((o) => Math.hypot(o.col! - me().col!, o.row! - me().row!)));
log("put down", apart.toFixed(1), "tiles from the furthest of their party");
await screenshot("put_down_far");

// They find each other: all on one or neighbouring tiles.
const spread = () => Math.max(...others().map((o) => Math.hypot(o.col! - me().col!, o.row! - me().row!)));
await park.advanceUntil(() => spread() < 1.5 && me().state !== "lost", 100, `the party together again (${spread().toFixed(1)} tiles apart, ${me().state})`);
log("together after", apart.toFixed(1), "tiles apart; states", me().state, others().map((o) => o.state).join(","));
await screenshot("together");
expect(others().length > 0 && me().party === party, "still one party");
quit(0);
