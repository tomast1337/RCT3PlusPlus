// SPDX-License-Identifier: GPL-3.0-or-later
//
// Research in a scenario that budgets for it: over a few months attractions are invented, each told in a
// message and offered in the build lists, the money booked as research spending; the research window sets the
// budget and the areas' priorities; saved and opened again, research is where it was. A sandbox does none.
//
//   ./nob script scripts/tests/research.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

/** The path of the widget named `name` somewhere under `path`. */
function under(path: string, name: string): string | undefined {
    for (const child of ui.children(path) ?? []) {
        const at = `${path}/${child}`;
        if (child === name)
            return at;
        const found = under(at, name);
        if (found !== undefined)
            return found;
    }
    return undefined;
}

await park.open("Campaigns/Base/Gunslinger.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined, 1200, "the park");
await until(() => park.research().left !== null, 1200, "the items left to research");
const start = park.research();
log("research at first:", JSON.stringify(start));
expect(start.budget === 20000, `the scenario's budget, not ${start.budget}`);
expect(start.spent > 0, "the money the scenario had already spent on its item");
expect(start.current !== null, "the item the scenario was researching");
expect(start.invented.length === 0, "nothing invented yet");
expect((start.left ?? 0) > 3, `several items left, not ${start.left}`);
expect(build.offers(start.current!) === false, `${start.current} not offered before it is invented`);

// Three months, each set on by hand: research goes with the calendar.
const cash = park.cash()!;
for (let month = 0; month < 3; ++month) {
    park.setDate(park.date() + 360);
    await frames(10);
}
const after = park.research();
log("research after three months:", JSON.stringify(after));
expect(after.invented.length >= 1, "an attraction invented");
expect(after.invented[0] === start.current, `the scenario's item ${start.current} invented first, not ${after.invented[0]}`);
expect(after.left === start.left! - after.invented.length, "each invention off the list of what is left");
expect(after.current !== null && !after.invented.includes(after.current), "another item under way");
expect(park.ledger().research > 0, "research spending in the month's ledger");
expect(park.cash()! < cash, "the cash less what research cost");
const told = park.messages().filter((m) => m.key === "Invented_Attraction");
log("messages:", JSON.stringify(told));
expect(told.length === after.invented.length, `a message for each invention, not ${told.length}`);
expect(told.every((m) => m.name.length > 0), "each message naming its attraction");
expect(after.invented.every((symbol) => build.offers(symbol!) === true), "each invention offered in the build lists");
expect(build.offers(after.current!) === false, `${after.current} not offered while it is researched`);

// No budget, no research: nothing is spent and nothing moves.
park.setResearch(0);
const stopped = JSON.stringify(park.research());
park.setDate(park.date() + 360);
await frames(10);
expect(JSON.stringify(park.research()) === stopped, "research standing still without a budget");
park.setResearch(20000);

// The research window: the budget's arrows and an area's bar set them, and it tells what is under way.
await ui.click(`tools/Management/${ui.children("tools/Management")![0]}`);
await until(() => ui.find("tools/Management/Man_Research") !== undefined, 120, "the management menu");
await ui.click("tools/Management/Man_Research");
await until(() => under("Research_Title", "Research_Label_Budget") !== undefined, 120, "the research window");
await frames(70); // A page written again, now the window has drawn once
const current = ui.find(under("Research_Title", "Rct3pp_Research_Current_value")!)!;
const left = ui.find(under("Research_Title", "Rct3pp_Research_MonthsLeft_label")!)!;
log("window:", current.text, "/", left.text);
expect(current.text !== undefined && current.text.length > 1, `the item under way named, not ${current.text}`);
expect(left.text !== undefined && /\d|month/.test(left.text), `the months left, not ${left.text}`);
await screenshot("research_window");
const arrows = ui.find(under("Research_Title", "Research_Label_Budget")!)!;
await pointer.click(arrows.x, arrows.top + (arrows.bottom - arrows.top) / 4); // The upper arrow
await frames(5);
expect(park.research().budget === 25000, `a step of 50.00 more, not ${park.research().budget}`);
const thrill = ui.find(under("Research_Title", "Obj_Rides_ThrillRidesHelp")!)!;
await pointer.click(thrill.left + (thrill.right - thrill.left) * 0.8, thrill.y);
await frames(5);
log("priorities:", JSON.stringify(park.research().priorities));
expect(Math.abs(park.research().priorities[2] - 0.8) < 0.03, `thrill rides at about 80 %, not ${park.research().priorities[2]}`);
await frames(70);
expect(ui.find(under("Research_Title", "Research_Label_Budget_value")!)!.text!.includes("250"), "the window showing the new budget");
await ui.click("Research_Title/close");
await until(() => ui.find("Research_Title") === undefined, 120, "the research window to close");

// Saved and opened again, paused so nothing moves between: research as it was.
await ui.click("bar/Top_Pause");
await frames(5);
const before = JSON.stringify(park.research());
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
await until(() => park.research().left !== null, 1200, "the saved park's items left");
log("research reloaded:", JSON.stringify(park.research()));
expect(JSON.stringify(park.research()) === before, `research as saved, not ${JSON.stringify(park.research())}`);
expect(after.invented.every((symbol) => build.offers(symbol!) === true), "each invention still offered");

// A sandbox budgets nothing and invents nothing.
await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined, 1200, "the sandbox");
await frames(30);
park.setDate(park.date() + 360);
await frames(10);
expect(park.research().budget === 0 && park.research().invented.length === 0, "no research in a sandbox");
quit(0);
