// SPDX-License-Identifier: GPL-3.0-or-later
//
// Parties at the rides, on a scenario with rides built and guests added. With the park running:
//
//   - two or more of a party ride on one load together;
//   - two of a party in a queue stand side by side across the line;
//   - those of a party staying off a ride wait off its exit's tile.
//
//   ./nob script scripts/tests/party_rides.ts -- --no-audio

export {};

await park.open("Campaigns/Base/VanillaHills.dat", { sandbox: false });
await park.ready();
expect(debug.use("add_guests", 400) > 0, "guests added");

type Guest = WalkedPerson;
/** The guests in `state`, by their party, for parties with two or more of them so. */
const byParty = (state: PersonState) => {
    const parties = new Map<number, Guest[]>();
    for (const p of people.list())
        if (p.state !== undefined && p.state === state && p.party !== undefined)
            parties.set(p.party, [...(parties.get(p.party) ?? []), p]);
    return [...parties.values()].filter((ps) => ps.length >= 2);
};
const middle = (ps: Guest[]) => ({
    col: ps.reduce((n, p) => n + p.col, 0) / ps.length,
    row: ps.reduce((n, p) => n + p.row, 0) / ps.length,
});

// Riding together: two or more of one party on the same ride at once.
const together = () =>
    byParty("riding").find((ps) =>
        ps.every(
            (p) =>
                p.state === "riding" && p.ride !== undefined && p.ride === (ps[0] as Guest & { state: "riding" }).ride,
        ),
    );
await park.advanceUntil(() => together() !== undefined, 900, "a party riding together");
const riders = must(together(), "a party riding");
const ride = (riders[0] as Guest & { state: "riding" }).ride!;
log("riding together", riders.length, "of party", riders[0].party, "on", rides.list()[ride].symbol);

// Side by side in a queue: two of a party next to each other in the line.
const pair = () => byParty("queueing").map((ps) => [ps[0], ps[1]])[0];
await park.advanceUntil(() => pair() !== undefined, 600, "two of a party side by side in a queue");
const [a, b] = must(pair(), "a pair queueing");
const apart = Math.hypot(a.col - b.col, a.row - b.row);
log("queueing pair", a.party, "apart", apart.toFixed(2), "tiles");
expect(apart < 1, "two of a party queue within a tile of each other");
const q = middle([a, b]);
camera.look({ ...q, up: 0.8 }, { col: q.col + 0.8, row: q.row + 1.6, up: 2.5 });
await frames(10);
await screenshot("party_rides_queue_pair");

// Waiting by a ride's exit: off the exit's own tile.
await park.advanceUntil(() => byParty("waiting").length > 0, 600, "a party waiting by a ride");
const exits = rides.list().flatMap((r) => (r.exitAt ? [r.exitAt] : []));
const onExit = people
    .list()
    .filter(
        (p) => p.state === "waiting" && exits.some((e) => Math.floor(p.col) === e.col && Math.floor(p.row) === e.row),
    );
log("waiting", people.list().filter((p) => p.state === "waiting").length, "on exit tiles", onExit.length);
quit(0);
