// SPDX-License-Identifier: GPL-3.0-or-later
//
// Riding along is heard from the seat: a coaster is ridden with the ride camera and a few seconds recorded with
// sound, in which the train's own wheels and chain play close by rather than from where the park camera was.
// The ride is the one given after a second `--` (its place in rides.list()), else the first with trains, in the
// park given with --park, else in the game's Vanilla Hills. Compare the loudness of ride_sound.mp4 in its report
// folder with ffmpeg's volumedetect.
//
//   ./nob script scripts/tests/ride_camera_sound.ts -- --audio --park PARK -- RIDE

export {};

const [given] = args();
const seconds = 8;
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
const first = camera.riding()!;
await until(
    () => Math.hypot(...camera.riding()!.eye!.map((v, i) => v - first.eye![i])) > 1,
    600,
    "the car moving the view",
);
record.start({ path: "ride_sound.mp4", fps: 30, audio: true });
await frames(30 * seconds);
const r = await record.stop();
log("recorded", JSON.stringify(r));
expect(r.problem === undefined, `no problem, not ${r.problem}`);
expect(r.samples === seconds * r.sampleRate, `${seconds} s of sound, not ${r.samples} sound frames`);
expect(camera.riding()?.ride === ride, "still riding that coaster");
