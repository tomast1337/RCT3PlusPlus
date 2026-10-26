// SPDX-License-Identifier: GPL-3.0-or-later
//
// How guests go about the game's parks: on Vanilla Hills' paths not all at one pace, some running and the rest
// walking; on Gunslinger's mine coaster riders wait in the station and up the lift, and throw their hands up on the way
// round. Each is looked at and pictured.
//
//   ./nob script scripts/tests/guest_motion.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

function count<T>(list: T[], test: (item: T) => boolean): number {
    return list.filter(test).length;
}

await park.open("Campaigns/Base/VanillaHills.dat");
await park.ready();
await frames(60);

// Guests on the paths: some running, most walking.
await until(() => people.list().some((p) => p.state === "walking" && p.gait === "run"), 3600, "a guest running");
const everyone = people.list();
const walkers = everyone.filter((p) => p.state === "walking" && p.gait !== undefined);
log("gaits", ["walk", "run", "weary", "queasy", "bursting"].map((g) => `${g} ${count(walkers, (p) => p.gait === g)}`).join(", "));
expect(count(walkers, (p) => p.gait === "run") < walkers.length / 2, "most guests walking, not running");
// Followed from behind (renamed with the game's cheat name, as the ride camera does), each pictured.
async function behind(person: number, name: string): Promise<void> {
    people.rename(person, "Jonny Watts");
    await until(() => camera.riding()?.eye !== undefined, 60, `the view from ${name}`);
    await ui.click("ride_camera/RidecamGUI_Type");
    await frames(2);
    expect(camera.riding()!.kind === "behind", `behind ${name}`);
    pointer.move(0.5, 0.5);
    pointer.wheel(-3);
    await frames(10);
    await screenshot(name);
    await ui.click("ride_camera/RidecamGUI_Exit");
    await frames(5);
}
await behind(everyone.findIndex((p) => p.state === "walking" && p.gait === "run"), "running guest");

// Riders on Gunslinger's mine coaster: waiting at the station or on the lift, and hands up on the way round.
await park.open("Campaigns/Base/Gunslinger.dat");
await park.ready();
await frames(60);
const coasters = rides.list().map((r, i) => ({ r, i })).filter(({ r }) => !r.flat && (r.trains ?? 0) > 0);
expect(coasters.length > 0, "a coaster in the park");
await until(() => people.list().some((p) => p.reaction === "hands up"), 7200, "a rider with their hands up");
const riders = people.list().filter((p) => p.reaction !== undefined);
log("reactions", ["waiting", "calm", "hands up", "covering eyes", "strained"].map((r) => `${r} ${count(riders, (p) => p.reaction === r)}`).join(", "));
await until(() => people.list().some((p) => p.reaction === "waiting"), 7200, "riders waiting in a station or up a lift");
// Paused once a rider has their hands up, so they keep them up for the picture.
await until(() => people.list().some((p) => p.reaction === "hands up"), 7200, "a rider with their hands up again");
await ui.click("bar/Top_Pause");
await frames(2);
const rider = people.list().findIndex((p) => p.reaction === "hands up");
expect(rider >= 0, "the rider's hands still up once paused");
await behind(rider, "rider with hands up");
