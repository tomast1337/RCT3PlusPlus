// SPDX-License-Identifier: GPL-3.0-or-later
//
// The research window's other tabs: what has been invented and when, and what may come next, where the player
// focuses research on an item of their choosing at a higher cost. A focus starts on the item at once; ended, what
// it took the place of is taken up again. Saved and opened again, the dates and the focus are kept.
//
//   ./nob script scripts/tests/research_focus.ts -- --no-audio

export {};

/** The path of the widget named `name` somewhere under `path`. */

await park.open("Campaigns/Base/Gunslinger.dat", { sandbox: false });
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined, 1200, "the park");
await until(() => park.research().left !== null, 1200, "the items left to research");
park.setResearch(
    20000,
    Array.from({ length: 12 }, (_, i) => (i === 8 || i === 9 ? 0 : 1)),
);

// Two months on: each invention has the date it was made.
const began = park.date();
for (let month = 0; month < 2; ++month) {
    park.setDate(park.date() + 360);
    await frames(10);
}
const after = park.research();
log("research after two months:", JSON.stringify(after));
expect(after.invented.length >= 1, "something invented");
expect(after.inventedAt.length === after.invented.length, `a date for each invention, not ${after.inventedAt.length}`);
expect(
    after.inventedAt.every((t, i) => t >= began && t <= park.date() && (i === 0 || t >= after.inventedAt[i - 1])),
    `the dates in order within the two months, not ${JSON.stringify(after.inventedAt)}`,
);
expect(after.next.length > 0 && after.next.length <= 36, `a few items to come, not ${after.next.length}`);
expect(!after.next.includes(after.current), "the item under way not among those to come");

// Focus by script: the item is worked on at once; ended, the one it replaced comes back.
expect(park.focusResearch("NoSuchItem") === false, "no focus on an item not left to research");
const dropped = after.current!;
const chosen = after.next[0]!;
expect(park.focusResearch(chosen), `a focus on ${chosen}`);
expect(park.research().focus === chosen && park.research().current === chosen, `${chosen} focused and under way`);
expect(park.focusResearch(null), "the focus ended");
park.setDate(park.date() + 12);
await frames(5);
log("after the focus ended:", JSON.stringify(park.research()));
expect(
    park.research().focus === null && park.research().current === dropped,
    `${dropped} taken up again, not ${park.research().current}`,
);

// The research window: the history tab lists each invention with its date.
await ui.click(`tools/Management/${must(ui.children("tools/Management"), "tools/Management shown")[0]}`);
await until(() => ui.find("tools/Management/Man_Research") !== undefined, 120, "the management menu");
await ui.click("tools/Management/Man_Research");
await until(() => under("Research_Title", "Research_Label_Budget") !== undefined, 120, "the research window");
await ui.click(
    must(
        under("Research_Title", "AttractionListDialogIconAllAttractions"),
        "AttractionListDialogIconAllAttractions under Research_Title",
    ),
);
await frames(70);
await screenshot("research_invented");

// The tab of what may come next: a row picked names the focus's cost on the button, which focuses on it.
await ui.click(must(under("Research_Title", "Obj_Rides"), "Obj_Rides under Research_Title"));
await frames(10);
const focusButton = () =>
    must(
        ui.find(must(under("Research_Title", "Rct3pp_Research_Focus"), "Rct3pp_Research_Focus under Research_Title")),
        "Rct3pp_Research_Focus shown",
    );
expect(
    !/\d/.test(focusButton().text ?? "0"),
    `no cost on the focus button with nothing picked, not ${focusButton().text}`,
);
const frame = must(ui.find("Research_Title"), "Research_Title shown");
const down = (frame.bottom - frame.top) / 380; // Of the screen's height, a unit of the window's
const table = must(ui.find(must(under("Research_Title", "table"), "table under Research_Title")), "table shown");
// The second row
await pointer.click(table.left + (table.right - table.left) / 3, table.top + (3 + 18 + 16 + 8) * down);
await frames(5);
log("focus button:", JSON.stringify(focusButton()));
expect(/\d/.test(focusButton().text ?? ""), `the focus button naming a cost, not ${focusButton().text}`);
await screenshot("research_coming");
const listed = park.research().next[1]!;
await ui.click(must(under("Research_Title", "Rct3pp_Research_Focus"), "Rct3pp_Research_Focus under Research_Title"));
await frames(70);
log("focused:", JSON.stringify(park.research()));
expect(
    park.research().focus === listed && park.research().current === listed,
    `the second row's ${listed} focused, not ${park.research().focus}`,
);
await screenshot("research_focused");

// Saved and opened again, paused so nothing moves between: the dates and the focus as they were.
await ui.click("Research_Title/close");
await until(() => ui.find("Research_Title") === undefined, 120, "the research window to close");
await ui.click("bar/Top_Pause");
await frames(5);
const before = JSON.stringify(park.research());
await files_menu("File_SaveGame");
await until(() => ui.find("park_files") !== undefined, 120, "the save window");
await ui.click("park_files/content/open");
await until(() => ui.find("park_files") === undefined, 120, "the save window to close");
await files_menu("File_LoadGame");
// Loading asks first whether to save; the park was saved just now.
await until(() => ui.find("load_question") !== undefined, 120, "the question before loading");
await ui.click("load_question/content/answer1");
await until(
    () => ui.find("park_files/content/list/view/content/row0") !== undefined,
    120,
    "the save in the load window",
);
await ui.click("park_files/content/list/view/content/row0");
await ui.click("park_files/content/open");
await frames(5);
await until(() => park.loaded(), 1200, "the saved park");
await until(() => park.research().left !== null, 1200, "the saved park's items left");
log("research reloaded:", JSON.stringify(park.research()));
expect(JSON.stringify(park.research()) === before, `research as saved, not ${JSON.stringify(park.research())}`);

// The focused item invented, research goes back to what it replaced.
await ui.click("bar/Top_Pause");
for (let i = 0; i < 12 && !park.research().invented.includes(listed); ++i) {
    park.setDate(park.date() + 60);
    await frames(5);
}
log("after the focus:", JSON.stringify(park.research()));
expect(park.research().invented.includes(listed), `${listed} invented`);
expect(park.research().focus === null, "no focus left");
expect(
    park.research().current === dropped || park.research().invented.includes(dropped),
    `${dropped} taken up again after the focus`,
);
quit(0);
