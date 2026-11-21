// SPDX-License-Identifier: GPL-3.0-or-later
//
// Parties at the rides, on a scenario with rides built and guests added. With the park running:
//
//   - two or more of a party ride on one load together;
//   - two of a party in a queue stand side by side across the line;
//   - those of a party staying off a ride wait together beside its exit, off the exit's own tile, and think so;
//   - parties crowding round one ride spread over the tiles beside it, none standing in another's place.
//
// Started on a saved park that has guests of its own (the viewer's `--park PATH`, and `-- --saved` for the
// script), it runs there instead.
//
//   ./nob script scripts/tests/party_rides.ts -- --no-audio [--park PATH -- --saved]

export {};

const saved = args().includes("--saved");
if (!saved) await park.open("Campaigns/Base/VanillaHills.dat", { sandbox: false });
await until(() => park.loaded(), 1200, "the park");
await park.ready();
if (!saved) expect(debug.use("add_guests", 400) > 0, "guests added");

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
    up: ps.reduce((n, p) => n + p.up, 0) / ps.length,
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
const on = middle(riders);
camera.look({ ...on, up: on.up + 1 }, { col: on.col + 5, row: on.row + 7, up: on.up + 14 });
await frames(10);
await screenshot("party_rides_riding_together");

// Side by side in a queue: two of a party next to each other in the line.
const pair = () => byParty("queueing").map((ps) => [ps[0], ps[1]])[0];
await park.advanceUntil(() => pair() !== undefined, 600, "two of a party side by side in a queue");
const [a, b] = must(pair(), "a pair queueing");
const apart = Math.hypot(a.col - b.col, a.row - b.row);
log("queueing pair", a.party, "apart", apart.toFixed(2), "tiles");
expect(apart < 1, "two of a party queue within a tile of each other");
const q = middle([a, b]);
camera.look({ ...q, up: q.up + 0.8 }, { col: q.col + 3, row: q.row + 5, up: q.up + 12 });
await frames(10);
await screenshot("party_rides_queue_pair");

// Waiting by a ride's exit: two or more of a party together beside it, off the exit's own tile. Those a saved
// park left waiting stand where it put them, so after a while.
await park.advance(120);
const kWaitingForFriends = 256;
const exits = rides.list().flatMap((r) => (r.exitAt ? [r.exitAt] : []));
const onExit = (p: Guest) => exits.some((e) => Math.floor(p.col) === e.col && Math.floor(p.row) === e.row);
const byExit = (p: Guest) => exits.some((e) => Math.hypot(p.col - (e.col + 0.5), p.row - (e.row + 0.5)) < 2);
const ring = () => {
    const off = byParty("waiting").filter((ps) => ps.every((p) => byExit(p) && !onExit(p)));
    return off.find((ps) => ps.length >= 3) ?? off[0];
};
await park.advanceUntil(() => ring() !== undefined, 600, "a party waiting beside a ride's exit, off it");
const waiters = must(ring(), "a party waiting");
// Riders just off step out over the exit's tile on their way to the others, so a few are on one at any time.
const waiting = people.list().filter((p) => p.state === "waiting") as Guest[];
const onExits = waiting.filter((p) => onExit(p as Guest)).length;
log("waiting", waiting.length, "on exit tiles", onExits);
log("party", waiters[0].party, "waits", waiters.length, "beside an exit");
expect(onExits <= waiting.length / 10, "hardly any of those waiting on an exit's tile");
// Where parties crowd round a busy ride, two on one tile stand each in a ring of its own, not in one another.
const piled = waiting.filter((p, i) =>
    waiting.some((q, j) => j < i && Math.hypot(p.col - q.col, p.row - q.row) < 0.02),
);
log("waiting guests standing right where another does", piled.length);
expect(piled.length <= waiting.length / 20, "hardly any of those waiting stand in another's place");
const thinking = people.list().filter((p) => !p.staff && p.thoughts.includes(kWaitingForFriends)).length;
log("guests thinking they wait for their friends", thinking);
expect(thinking > 0, "a waiting guest thinks so");
const w = middle(waiters);
camera.look({ ...w, up: w.up + 0.8 }, { col: w.col + 1.6, row: w.row + 2.4, up: w.up + 3 });
await frames(10);
await screenshot("party_rides_waiting_ring");
quit(0);
