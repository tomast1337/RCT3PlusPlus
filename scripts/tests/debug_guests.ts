// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Debug tab's guest switches on a scenario full of added guests: slow motion makes them cover less ground in
// the same park time, groups that stand together often leave more of them standing, and the motions played in
// turn, by everyone or by the person whose window is open. Keeps a screenshot of the tab with the switches.
//
//   ./nob script scripts/tests/debug_guests.ts -- --no-audio

export {};

await park.open("Campaigns/Base/VanillaHills.dat", { sandbox: false });
await park.ready();
expect(debug.use("add_guests", 400) > 0, "guests added");
await park.advance(20);

type Walker = Person & { state: "walking" };
const walkers = () => people.list().filter((p): p is Walker => p.state === "walking");

// How far, in tiles, the walkers went in `seconds` on average, as the crow flies.
const ground = async (seconds: number) => {
    const from = new Map(walkers().map((p) => [p.name, p]));
    await park.advance(seconds);
    const moved = walkers().flatMap((p) => {
        const was = from.get(p.name);
        return was ? [Math.hypot(p.col - was.col, p.row - was.row)] : [];
    });
    return moved.reduce((a, b) => a + b, 0) / Math.max(moved.length, 1);
};
// The share of the walkers in a group who stood where they were for `seconds`.
const standing = async (seconds: number) => {
    const grouped = walkers().filter((p) => p.party !== undefined);
    const from = new Map(grouped.map((p) => [p.name, p]));
    await park.advance(seconds);
    const still = walkers().filter((p) => {
        const was = from.get(p.name);
        return was && Math.hypot(p.col - was.col, p.row - was.row) < 0.05;
    });
    return still.length / Math.max(grouped.length, 1);
};

// Slow motion: the same park time takes them a fraction as far.
const pace = await ground(10);
expect(debug.use("slow_guests", 4) === 1, "slow motion on");
expect(debug.use("slow_guests", 4) === 0, "slow motion already on");
const slowed = await ground(10);
log("tiles in 10 s", pace, "slowed", slowed);
expect(slowed < pace * 0.6, `slowed guests go less far (${slowed} against ${pace})`);
expect(debug.use("slow_guests", 1) === 1, "slow motion off");

// Groups standing together at every rest leave more of them standing.
const before = await standing(3);
expect(debug.use("parties_huddle_often", 1) === 1, "groups stand together often");
await park.advance(15);
const often = await standing(3);
log("grouped walkers standing", before, "often", often);
expect(often > before, `more groups standing (${often} against ${before})`);
expect(debug.use("parties_huddle_often", 0) === 1, "groups back to their own habits");

// Every motion in turn, for everyone, then for the person whose window is open.
expect(debug.use("cycle_person_motions", 1) === 0, "no person to cycle without a window open");
expect(debug.use("cycle_motions", 1) === 1, "everyone cycles their motions");
expect(debug.use("cycle_motions", 1) === 0, "everyone already cycling");
await frames(30);
await screenshot("everyone_cycling");
const shown = walked(walkers()[0], "a walking guest");
people.show(shown.index);
await frames(10);
expect(debug.use("cycle_person_motions", 1) === 1, "the shown person cycles their motions");
expect(debug.use("cycle_motions", 0) === 0, "everyone no longer cycling");
await frames(30);
expect(debug.use("cycle_person_motions", 0) === 1, "the shown person back to their own motions");

expect(debug.use("slow_guests", 4) === 1, "slow motion on for the tab's picture");
expect(debug.use("parties_huddle_often", 1) === 1, "groups standing for the tab's picture");
keys.press("F12");
await frames(2);
debug.open();
await frames(10);
await screenshot("debug_tab");
quit(0);
