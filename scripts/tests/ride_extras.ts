// SPDX-License-Identifier: GPL-3.0-or-later
//
// A ride's extras, changed from its window in the game's Vanilla Hills: its fence button shows the looks its fence,
// entrance and exit may take, and each one picked shows in the park at once. A tune picked plays near the ride
// while it is open, and all of it is still there once the park is saved and loaded again.
//
//   ./nob script scripts/tests/ride_extras.ts -- --audio

export {};

await park.open("Campaigns/Base/VanillaHills.dat");
await park.ready();
await until(() => build.flatRides() !== undefined, 1200, "the build lists");
await ui.click("bar/Top_Pause");

const ride = must(
    rides
        .list()
        .find(
            (r) => !r.demolished && r.entrance !== null && r.exit !== null && r.fence !== "" && r.entranceItem !== "",
        ),
    "a ride with a fence and both doors",
).index;
const now = () => rides.list()[ride];
log("ride", now().name, "fence", now().fence, "doors", now().entranceItem, now().exitItem, "music", now().music);

/** Picks the first item of the list `name` on the ride's window, or the last when the first is what it has. */
async function pickOther(name: string, has: () => string): Promise<string> {
    const before = has();
    // The window shows only a few rows: scroll the list into full view.
    const body = must(ui.find("info/body"), "the window's body");
    const list = must(ui.find(`info/body/${name}`), name);
    pointer.move(body.x, body.y);
    pointer.wheel(list.bottom > body.bottom ? -3 : list.top < body.top ? 3 : 0);
    await frames(3);
    for (const key of ["Home", "End"]) {
        await ui.click(`info/body/${name}`);
        await frames(3);
        keys.press(key);
        await frames(1);
        keys.press("Enter");
        await frames(5);
        if (has() !== before) break;
    }
    expect(has() !== before, `${name} changed from ${before}`);
    return has();
}

rides.show(ride);
await until(() => ui.find("info/body/Attraction_General_PickFence") !== undefined, 120, "the ride's window");
expect(ui.find("info/body/fence_style") === undefined, "the looks hidden until the fence button is pressed");
await ui.click("info/body/Attraction_General_PickFence");
await until(() => ui.find("info/body/fence_style") !== undefined, 60, "the looks to pick from");
expect(
    ui.find("info/body/entrance_style") !== undefined && ui.find("info/body/exit_style") !== undefined,
    "door looks too",
);

// The queue's railings before, which the new fence replaces.
const queueBefore = (() => {
    const d = now().entrance!;
    const close = (o: { col: number; row: number }) => Math.abs(o.col - d.col) <= 1 && Math.abs(o.row - d.row) <= 1;
    const ids = new Set(map.paths().flatMap((p) => (p.queue && p.line !== undefined && close(p) ? [p.line] : [])));
    return map.paths().filter((p) => p.queue && p.line !== undefined && ids.has(p.line));
})();
const onQueue = () => map.objects().filter((o) => queueBefore.some((p) => p.col === o.col && p.row === o.row)).length;
const standingBefore = onQueue();
log(
    "queue railings before",
    JSON.stringify(queueBefore.map((p) => (p.queue ? p.railing : ""))),
    "objects on it",
    standingBefore,
);
const fence = await pickOther("fence_style", () => now().fence);
const entrance = await pickOther("entrance_style", () => now().entranceItem);
const exit = await pickOther("exit_style", () => now().exitItem);
log("now fence", fence, "doors", entrance, exit);
await screenshot("ride_window");

// The park shows them: the new fence stands beside the entrance, and the doors are placed as the new items.
const door = now().entrance!;
const near = (o: { col: number; row: number }) => Math.abs(o.col - door.col) <= 1 && Math.abs(o.row - door.row) <= 1;
await frames(10);
expect(
    map.objects().some((o) => o.symbol.toLowerCase() === fence.toLowerCase() && near(o)),
    `${fence} panels beside the entrance`,
);
expect(
    map.objects().some((o) => o.symbol === entrance && near(o)),
    `the entrance shown as ${entrance}`,
);
// Its queue is railed with the new fence too.
const lines = new Set(map.paths().flatMap((p) => (p.queue && p.line !== undefined && near(p) ? [p.line] : [])));
const queue = map.paths().filter((p) => p.queue && p.line !== undefined && lines.has(p.line));
log("queue railings", JSON.stringify(queue.map((p) => (p.queue ? p.railing : ""))));
expect(queue.length > 0, "a queue beside the entrance");
expect(
    queue.every((p) => p.queue && p.railing.toLowerCase() === fence.toLowerCase()),
    `the queue railed with ${fence}`,
);
expect(
    map
        .objects()
        .some(
            (o) =>
                o.symbol.toLowerCase() === fence.toLowerCase() && queue.some((p) => p.col === o.col && p.row === o.row),
        ),
    `${fence} railings on the queue`,
);
expect(
    onQueue() === standingBefore,
    `the old railings replaced, not kept: ${standingBefore} then ${onQueue()} on the queue`,
);
camera.look(
    { col: queue[0].col + 0.5, row: queue[0].row + 0.5 },
    { col: queue[0].col + 3.5, row: queue[0].row + 4.5, up: 3 },
);
await frames(10);
await screenshot("queue_railed");

// A tune, heard near the open ride.
const tune = await pickOther("AttractionViewDialogMusicToolTip", () => now().music);
log("tune", tune);
expect(rides.setState(ride, "open") === null || now().joined, "the ride open");
camera.look({ col: door.col + 0.5, row: door.row + 0.5 }, { col: door.col + 6.5, row: door.row + 8.5, up: 8 });
await ui.click("bar/Top_Pause");
await until(
    () => rides.music().some((m) => m.file.toLowerCase() === tune.toLowerCase() && m.volume > 0),
    300,
    `${tune} heard near the ride, not ${JSON.stringify(rides.music())}`,
);
log("heard", JSON.stringify(rides.music()));
await ui.click("bar/Top_Pause");
await screenshot("ride_restyled");

// Saved and loaded again.
await click_tool("Files");
await ui.click("tools/Files/File_SaveGame");
await until(() => ui.find("park_files") !== undefined, 120, "the save window");
await ui.click("park_files/content/open");
await until(() => ui.find("park_files") === undefined, 120, "the save window to close");
if (!ui.find("tools/Files/File_LoadGame")) await click_tool("Files");
await until(() => ui.find("tools/Files/File_LoadGame") !== undefined, 120, "the files menu");
await ui.click("tools/Files/File_LoadGame");
await until(() => ui.find("load_question") !== undefined, 120, "the question before loading");
await ui.click("load_question/content/answer1");
const first = "park_files/content/list/view/content/row0";
await until(() => ui.find(first) !== undefined, 120, "the save in the load window");
await ui.click(first);
await ui.click("park_files/content/open");
await until(
    () => park.loaded() && ui.find("park_files") === undefined && rides.list().length > ride,
    1200,
    "the saved park",
);
await frames(30);
const back = now();
log("after loading", back.fence, back.entranceItem, back.exitItem, back.music);
expect(back.fence === fence, `the fence kept, not ${back.fence}`);
expect(
    back.entranceItem === entrance && back.exitItem === exit,
    `the doors' looks kept, not ${back.entranceItem}, ${back.exitItem}`,
);
expect(back.music === tune, `the tune kept, not ${back.music}`);
expect(
    map.objects().some((o) => o.symbol.toLowerCase() === fence.toLowerCase() && near(o)),
    `${fence} panels beside the entrance after loading`,
);
const queueBack = map.paths().filter((p) => p.queue && queue.some((q) => q.col === p.col && q.row === p.row));
expect(
    queueBack.length === queue.length &&
        queueBack.every((p) => p.queue && p.railing.toLowerCase() === fence.toLowerCase()),
    `the queue still railed with ${fence} after loading`,
);
quit(0);
