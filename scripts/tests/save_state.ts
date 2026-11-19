// SPDX-License-Identifier: GPL-3.0-or-later
//
// A park from the game, opened: a box tells what its save holds that is not brought over, and the log lists it.
// Saved and loaded again, the park keeps its litter, the guests in its queues and those on its rides, and no box
// is told for a park of ours. Give it a park the game saved with litter and guests queueing:
//
//   ./nob script scripts/tests/save_state.ts -- --no-intro --no-audio --seed 1 --left-out-box --park PARK

export {};

await until(() => park.loaded(), 1200, "the park");
await until(() => ui.find("prize_won") !== undefined, 600, "the box telling what was left out");
const text = must(ui.find("prize_won/content/text"), "the box's text").text;
log("left out", JSON.stringify(text));
expect(text !== undefined && text.includes("\n- "), "the box listing what was left out");
await frames(5);
await screenshot("save_state_left_out");
await ui.click(`prize_won/${must(ui.children("prize_won"), "prize_won shown").at(-1)!}`);
await until(() => ui.find("prize_won") === undefined, 120, "the box to close");

await ui.click("bar/Top_Pause");
const litter = () =>
    JSON.stringify(
        park
            .litter()
            .map((l) => `${l.col},${l.row},${l.sick}`)
            .sort(),
    );
const in_state = (state: string) => () =>
    people
        .list()
        .filter((p) => p.state === state)
        .map((p) => p.name)
        .sort()
        .join("|");
const queued = in_state("queueing");
const riding = in_state("riding");
const saved_litter = litter();
const saved_queued = queued();
const saved_riding = riding();
log(
    "litter",
    park.litter().length,
    "queueing",
    saved_queued.split("|").filter((n) => n).length,
    "riding",
    saved_riding.split("|").filter((n) => n).length,
);

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
await until(() => park.loaded() && ui.find("park_files") === undefined, 1200, "the saved park");
await ui.click("bar/Top_Pause");
await frames(30);
expect(ui.find("prize_won") === undefined, "no box for a park of ours");
expect(litter() === saved_litter, `the litter as saved, ${park.litter().length} pieces now`);
const before = new Set(saved_queued.split("|"));
const after = new Set(queued().split("|"));
log(
    "queueing after loading",
    after.size,
    "lost",
    [...before].filter((n) => !after.has(n)).join(", "),
    "new",
    [...after].filter((n) => !before.has(n)).join(", "),
);
// Riders, not kept on their rides, may come back onto a queue's tiles and join it; no one queueing is lost.
expect(
    [...before].every((n) => after.has(n)),
    "everyone queueing as saved still queueing",
);
const riding_after = new Set(riding().split("|"));
const kept_riding = saved_riding.split("|").filter((n) => riding_after.has(n)).length;
log("riding after loading", riding_after.size, "of them riding as saved", kept_riding);
// Riders of a pool's rides, not kept, get off.
expect(kept_riding > 0 || saved_riding === "", "riders kept on their rides");
quit(0);
