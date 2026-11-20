// SPDX-License-Identifier: GPL-3.0-or-later
//
// Recording in game time: 2 s at 30 frames a second is 60 frames and exactly 2 s of sound, however long each
// frame took to draw and read back. Run it with each renderer:
//
//   ./nob script scripts/tests/record_frames.ts
//   ./nob script scripts/tests/record_frames.ts -- --renderer software

export {};

await park.open("Campaigns/Base/VanillaHills.dat");
await frames(10);
expect(!record.active(), "no recording before one starts");
record.start({ path: "two_seconds.mp4", fps: 30 });
expect(record.active(), "the recording about to start");
await frames(60); // A thirtieth of a game second each, while recording
const r = await record.stop();
log("recorded", JSON.stringify(r));
expect(r.problem === undefined, `no problem, not ${r.problem}`);
expect(r.frames === 60, `60 frames, not ${r.frames}`);
expect(r.samples === 2 * r.sampleRate, `2 s of sound, not ${r.samples} sound frames at ${r.sampleRate}`);
expect(r.seconds === 2, `a file 2 s long, not ${r.seconds}`);
expect(!record.active(), "no recording once it stopped");

// Asking for a window size waits for the window, and those frames are not counted.
record.start({ path: "sized.mp4", fps: 30, size: { width: 640, height: 360 } });
await frames(60);
const sized = await record.stop();
log("recorded", JSON.stringify(sized));
expect(sized.problem === undefined, `no problem, not ${sized.problem}`);
expect(sized.frames === 60, `60 frames at a new size, not ${sized.frames}`);
