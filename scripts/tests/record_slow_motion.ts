// SPDX-License-Identifier: GPL-3.0-or-later
//
// Slow motion: a game second taken at 240 frames and played at 30 is 240 real frames in a file 8 s long, its
// sound (with --audio) slowed along.
//
//   ./nob script scripts/tests/record_slow_motion.ts -- --audio

export {};

await park.open("Campaigns/Base/VanillaHills.dat");
await frames(10);
record.start({ path: "slow_motion.mp4", fps: 240, playbackFps: 30, audio: true });
await frames(240); // A game second
const r = await record.stop();
log("recorded", JSON.stringify(r));
expect(r.problem === undefined, `no problem, not ${r.problem}`);
expect(r.frames === 240, `240 frames, not ${r.frames}`);
expect(r.samples === r.sampleRate, `a second of sound, not ${r.samples} sound frames at ${r.sampleRate}`);
expect(r.seconds === 8, `a file 8 s long, not ${r.seconds}`);
