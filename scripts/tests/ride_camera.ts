// SPDX-License-Identifier: GPL-3.0-or-later
//
// Riding along: a ride with trains in the game's Vanilla Hills is opened, its window's ride camera clicked, and
// the view looks out from one of its cars, moving with it as it goes round. Everything else on screen is put
// away for the ride camera's panel, which slides out of sight while the pointer is still and back when it
// moves; leaving brings the rest back.
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
expect(ui.find("bar") === undefined && ui.find("tools") === undefined && ui.find("info") === undefined, "the rest put away");
const exit = "ride_camera/RidecamGUI_Exit";
expect(ui.find(exit) !== undefined, "the ride camera panel");
// Left still, away from the panel, it slides below the screen.
pointer.move(0.4, 0.4);
await until(() => (ui.find(exit)?.top ?? 1) >= 1, 3000, "the panel to hide"); // Its few seconds, at any frame rate
await screenshot("panel hidden");
pointer.move(0.6, 0.6);
await until(() => (ui.find(exit)?.bottom ?? 2) < 1, 120, "the panel back on a move");
// The type button goes round the kinds of camera the train has, each once, back to the first.
const kinds: string[] = [first.kind];
for (let i = 0; i < 3 && (i === 0 || camera.riding()!.kind !== first.kind); ++i) {
    await ui.click("ride_camera/RidecamGUI_Type");
    await frames(2);
    kinds.push(camera.riding()!.kind);
}
log("kinds", kinds.join(" "));
expect(kinds.length === 2 ? kinds[1] === first.kind : kinds[kinds.length - 1] === first.kind && new Set(kinds).size === kinds.length - 1, "each kind once, then the first again");
// Next train goes round every train of the park, ride after ride, and back to the first.
const tracked = rides.list().filter((r) => !r.flat && (r.trains ?? 0) > 0).length;
const start = camera.riding()!;
const stops: string[] = [];
for (let i = 0; i < 40; ++i) {
    await ui.click("ride_camera/RidecamGUI_NextTrain");
    await frames(2);
    const at = camera.riding()!;
    stops.push(`${at.ride}:${at.car}`);
    if (at.ride === start.ride && at.car === start.car)
        break;
}
log("trains", stops.join(" "));
expect(stops[stops.length - 1] === `${start.ride}:${start.car}` && new Set(stops).size === stops.length, "each train once, then back");
expect(tracked < 2 || new Set(stops.map((s) => s.split(":")[0])).size > 1, "other rides on the way");
// Dragging over the view turns the rider's head the way the pointer goes: to the left, and up.
await pointer.drag([0.6, 0.6], [0.4, 0.4], 10);
const turn = camera.riding()!.turn;
log("turn", JSON.stringify(turn));
expect(turn[0] > 0.1 && turn[1] > 0.1, "the head turned left and up");
await ui.click(exit);
await frames(5);
expect(camera.riding() === undefined, "the overhead view again");
expect(ui.find("bar") !== undefined && ui.find("tools") !== undefined, "the bar and tools back");
expect(ui.find("ride_camera") === undefined, "the panel gone");
await screenshot("overhead again");
quit(0);
