// SPDX-License-Identifier: GPL-3.0-or-later
//
// A park runs the same way whatever the frames: opened twice from one seed, once run on in big steps and once frame
// by frame, it has come to the same state at the same tick. Saved and loaded twice, it goes on the same way from
// each load. How far a load is from the run never saved is logged; the people's in-between states (half-eaten
// meals, rides mid-cycle) are not all kept yet, so the two are not held to be equal.
//
//   ./nob script scripts/tests/deterministic.ts -- --no-audio

export {};

const kPark = "Campaigns/Base/VanillaHills.dat";
const kTicksPerSecond = 4;

type Fingerprint = ReturnType<typeof park.fingerprint>;
const same = (a: Fingerprint, b: Fingerprint) => JSON.stringify(a) === JSON.stringify(b);

// Runs the park on to `tick` exactly: in big steps most of the way, then a frame at a time, as a frame runs at most
// one tick.
async function run_to(tick: number) {
    const left = tick - park.fingerprint().tick;
    if (left > 8) await park.advance((left - 8) / kTicksPerSecond);
    await until(() => park.fingerprint().tick >= tick, 1200, `tick ${tick}`);
    const at = park.fingerprint();
    expect(at.tick === tick, `the park at tick ${tick}, not ${at.tick}`);
    return at;
}

async function open() {
    await park.open(kPark);
    await frames(2);
}

// The same run in big steps and frame by frame.
await open();
await park.advance(120);
const stepped = park.fingerprint();
log("run in steps", JSON.stringify(stepped));
expect(stepped.tick >= 120 * kTicksPerSecond, `the park run on 120 s, not ${stepped.tick} ticks`);
await open();
await until(() => park.fingerprint().tick >= stepped.tick, 120 * 60 * 2, `tick ${stepped.tick} frame by frame`);
const framed = park.fingerprint();
log("run frame by frame", JSON.stringify(framed));
expect(same(framed, stepped), "the same park at the same tick, however it was run");

// Saved while paused, then run on, and loaded twice and run on as far.
await park.advance(30);
keys.press("P");
await frames(2);
const paused = park.fingerprint().tick;
await frames(2);
expect(park.fingerprint().tick === paused, "the park paused");
await click_tool("Files");
await ui.click("tools/Files/File_SaveGame");
await until(() => ui.find("park_files") !== undefined, 120, "the save window");
await ui.click("park_files/content/open");
await until(() => ui.find("park_files") === undefined, 120, "the save window to close");
keys.press("P");
await frames(2);
const target = paused + 240;
const unsaved = await run_to(target);
log("run on unsaved", JSON.stringify(unsaved));

async function load() {
    if (!ui.find("tools/Files/File_LoadGame")) await click_tool("Files");
    await until(() => ui.find("tools/Files/File_LoadGame") !== undefined, 120, "the files menu");
    await ui.click("tools/Files/File_LoadGame");
    await until(() => ui.find("load_question") !== undefined, 120, "the question before loading");
    await ui.click("load_question/content/answer1");
    await until(() => ui.find("park_files/content/list/view/content/row0") !== undefined, 120, "the save");
    await ui.click("park_files/content/list/view/content/row0");
    await ui.click("park_files/content/open");
    await frames(5);
    await until(() => park.loaded(), 1200, "the saved park");
    const loaded = park.fingerprint().tick;
    expect(loaded >= paused && loaded < target, `the park loaded from the tick saved, ${paused}, not ${loaded}`);
    return run_to(target);
}
const first = await load();
log("loaded, run on", JSON.stringify(first));
const second = await load();
log("loaded again, run on", JSON.stringify(second));
expect(same(first, second), "a save run on the same way each time it is loaded");
log("the loads' run", same(first, unsaved) ? "is" : "is not", "the run never saved");
