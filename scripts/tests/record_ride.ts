// SPDX-License-Identifier: GPL-3.0-or-later
//
// A video of a coaster ridden, the developer windows open around it: recorded in game time, it plays smoothly
// with its sound in step however slowly the machine draws that view. The ride is the one given after a second
// `--` (its place in rides.list()), else the first with trains, in the park given with --park, else in the
// game's Vanilla Hills; the length in game seconds may follow it (10).
//
//   ./nob script scripts/tests/record_ride.ts -- --audio --park PARK -- RIDE SECONDS

export {};

const [given, length] = args();
const seconds = Number(length ?? 10);
await frames(5);
if (!park.loaded()) await park.open("Campaigns/Base/VanillaHills.dat");
await park.ready();
await frames(60);
const ride =
    given !== undefined
        ? (Number(given) as RideIndex)
        : must(
              rides.list().find((r) => r.kind === "tracked" && r.trains > 0),
              "a ride with trains",
          ).index;
log("riding", rides.list()[ride].name);
rides.show(ride);
await frames(30);
await ui.click("info/body/RideCamGUI_Enabled");
await until(() => camera.riding()?.eye !== undefined, 120, "the view from a car");
keys.press("F12");
await frames(30);
record.start({ path: "ride.mp4", fps: 30, developerWindows: true });
await frames(30 * seconds);
const r = await record.stop();
log("recorded", JSON.stringify(r));
expect(r.problem === undefined, `no problem, not ${r.problem}`);
expect(r.frames === 30 * seconds, `${30 * seconds} frames, not ${r.frames}`);
expect(r.samples === seconds * r.sampleRate, `${seconds} s of sound, not ${r.samples} sound frames`);
