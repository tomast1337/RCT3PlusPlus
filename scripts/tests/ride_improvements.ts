// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ride improvements on the game's blank landscape: with corkscrews locked by a script, a looping coaster's
// construction panel greys its corkscrews, names the improvement they wait for in their help, and offers none where
// the track goes on. Research then invents corkscrews, with a message, and one is built. Saved and opened again, the
// park keeps corkscrews invented and loops, locked by then, still locked.
//
//   ./nob script scripts/tests/ride_improvements.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

const improvement = (id: string) => park.improvements().find((i) => i.id === id);
const corkscrewFits = () => (build.construction()?.fits ?? []).some((name) => /corkscrew/i.test(name));

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "tools");
expect(park.improvements().length > 0 && park.improvements().every((i) => i.invented), "every improvement invented in a park from the original game");
park.setImprovement("corkscrews", false);
expect(improvement("corkscrews")?.invented === false, "corkscrews locked");

expect(build.pick("tracks/coasters/LoopingRC/LoopingRC", "LoopingRC"), "a looping coaster to build");
await frames(5);
const ride = build.construction()!.ride;
const { width, height } = map.size();
const col = Math.floor(width / 2);
const row = Math.floor(height / 2);
camera.look({ col: col + 0.5, row: row + 0.5 }, { col: col + 0.5, row: row + 10.5, up: 20 });
await frames(5);
const at = map.screen(col, row)!;
pointer.move(at.x, at.y);
await frames(30);
await pointer.click(at.x, at.y);
await frames(30);
expect((rides.list()[ride].pieces ?? 0) > 0, "the station laid");
// Two straight pieces on from it, clear of the station's ends.
for (let i = 0; i < 2; ++i) {
    const had = rides.list()[ride].pieces ?? 0;
    for (let tries = 0; tries < 3 && (rides.list()[ride].pieces ?? 0) === had; ++tries) {
        await ui.click("placing/body/construction/AttractionEditor_IconCurve0");
        await frames(10);
    }
}
expect(!corkscrewFits(), `no corkscrew offered while corkscrews are locked, not ${JSON.stringify(build.construction()?.fits)}`);

// The named pieces' list: each row is looked at under the pointer till one says it waits for corkscrews, from the end,
// where the greyed rows are, scrolling up.
const list = "placing/body/construction/list";
const locked = async (): Promise<boolean> => {
    pointer.move(ui.find(list)!.x, ui.find(list)!.y);
    pointer.wheel(-100);
    await frames(2);
    for (let scroll = 0; scroll < 60; ++scroll) {
        const box = ui.find(list)!;
        for (let y = box.top + (box.bottom - box.top) / 48; y < box.bottom; y += (box.bottom - box.top) / 24) {
            pointer.move(box.left + (box.right - box.left) * 0.4, y);
            await frames(2);
            if ((ui.find(list)?.tooltip ?? "").includes("Corkscrews"))
                return true;
        }
        pointer.wheel(1);
        await frames(2);
    }
    return false;
};
expect(await locked(), "a corkscrew in the list whose help names the improvement it waits for");
await frames(45); // The help shows once the pointer rests
await screenshot("corkscrew_locked");

// Research invents corkscrews: with a budget far over its cost, within the month. A sandbox's money never runs out.
park.setResearch(100000000);
park.setDate(park.date() + 360);
await until(() => improvement("corkscrews")?.invented === true, 600, "corkscrews invented");
const told = park.messages().filter((m) => m.key === "Rct3pp_Improvement_Invented");
log("messages:", JSON.stringify(told));
expect(told.some((m) => m.name === "Corkscrews"), "a message telling corkscrews were invented");
park.setResearch(0);
park.setImprovement("loops", false);
await frames(5);
expect(corkscrewFits(), `a corkscrew offered once corkscrews are invented, not ${JSON.stringify(build.construction()?.fits)}`);

// The list's rows pressed in turn till a corkscrew is picked; pressed again, it is built.
const before = rides.list()[ride].pieces ?? 0;
const pick = async (): Promise<boolean> => {
    pointer.move(ui.find(list)!.x, ui.find(list)!.y);
    pointer.wheel(100);
    await frames(2);
    for (let scroll = 0; scroll < 60; ++scroll) {
        const box = ui.find(list)!;
        for (let y = box.top + (box.bottom - box.top) / 48; y < box.bottom; y += (box.bottom - box.top) / 24) {
            await pointer.click(box.left + (box.right - box.left) * 0.4, y);
            await frames(2);
            if (/corkscrew/i.test(build.construction()?.piece ?? "")) {
                await pointer.click(box.left + (box.right - box.left) * 0.4, y);
                await frames(5);
                return true;
            }
        }
        pointer.wheel(-1);
        await frames(2);
    }
    return false;
};
expect(await pick(), "a corkscrew picked from the list");
await frames(30);
const track = rides.list()[ride].track ?? [];
log("track:", JSON.stringify(track));
expect(rides.list()[ride].pieces === before + 1 && /corkscrew/i.test(track[track.length - 1] ?? ""), "a corkscrew built on the track");
await screenshot("corkscrew_built");

// Saved and opened again: corkscrews invented, loops locked, the corkscrew still there.
build.stop();
await frames(5);
const files = async (item: string) => {
    if (ui.find(`tools/Files/${item}`) === undefined)
        await ui.click(`tools/Files/${ui.children("tools/Files")![0]}`);
    await until(() => ui.find(`tools/Files/${item}`) !== undefined, 120, "the files menu");
    await ui.click(`tools/Files/${item}`);
};
await files("File_SaveGame");
await until(() => ui.find("park_files") !== undefined, 120, "the save window");
await ui.click("park_files/content/open");
await until(() => ui.find("park_files") === undefined, 120, "the save window to close");
await files("File_LoadGame");
await until(() => ui.find("park_files/content/list/view/content/row0") !== undefined, 120, "the save in the load window");
await ui.click("park_files/content/list/view/content/row0");
await ui.click("park_files/content/open");
await frames(5);
await until(() => park.loaded(), 1200, "the saved park");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined, 1200, "the saved park's tools");
expect(improvement("corkscrews")?.invented === true, "corkscrews still invented");
expect(improvement("loops")?.invented === false, "loops still locked");
expect(/corkscrew/i.test((rides.list()[ride].track ?? []).join(" ")), "the corkscrew still built");
quit(0);
