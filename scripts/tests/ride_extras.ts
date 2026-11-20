// SPDX-License-Identifier: GPL-3.0-or-later
//
// A ride's extras, changed from its window in the game's Vanilla Hills: its fence button shows the looks its fence,
// entrance and exit may take, and each one picked shows in the park at once. All of it is still there once the park
// is saved and loaded again.
//
//   ./nob script scripts/tests/ride_extras.ts

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
log("ride", now().name, "fence", now().fence, "doors", now().entranceItem, now().exitItem);

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

camera.look({ col: door.col + 0.5, row: door.row + 0.5 }, { col: door.col + 6.5, row: door.row + 8.5, up: 8 });
await frames(10);
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
log("after loading", back.fence, back.entranceItem, back.exitItem);
expect(back.fence === fence, `the fence kept, not ${back.fence}`);
expect(
    back.entranceItem === entrance && back.exitItem === exit,
    `the doors' looks kept, not ${back.entranceItem}, ${back.exitItem}`,
);
expect(
    map.objects().some((o) => o.symbol.toLowerCase() === fence.toLowerCase() && near(o)),
    `${fence} panels beside the entrance after loading`,
);
quit(0);
