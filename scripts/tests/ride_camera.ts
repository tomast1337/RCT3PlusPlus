// SPDX-License-Identifier: GPL-3.0-or-later
//
// Riding along: a ride with trains in the game's Vanilla Hills is opened, its window's ride camera clicked, and
// the view looks out from one of its cars, moving with it as it goes round.
//
//   ./nob script scripts/tests/ride_camera.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

await park.open("Campaigns/Base/VanillaHills.dat");
await park.ready();
await frames(60);
const ride = rides.list().findIndex((r) => !r.flat && (r.trains ?? 0) > 0);
expect(ride >= 0, "a ride with trains");
rides.show(ride);
await frames(30);
expect(rides.list()[ride].open === true, "the ride open");
expect(camera.riding() === undefined, "the overhead view first");
await screenshot("overhead");
await ui.click("info/body/RideCamGUI_Enabled");
await until(() => camera.riding()?.eye !== undefined, 60, "the view from a car");
const first = camera.riding()!;
log("riding", JSON.stringify(first));
expect(first.ride === ride, "a car of that ride");
const ahead = first.ahead!;
expect(Math.abs(Math.hypot(...ahead) - 1) < 0.1, "a way to look of length 1");
await screenshot("riding");
// A train under way is picked, so the view goes off with it.
await until(() => Math.hypot(...camera.riding()!.eye!.map((v, i) => v - first.eye![i])) > 1, 600, "the car moving the view");
await screenshot("riding on");
quit(0);
