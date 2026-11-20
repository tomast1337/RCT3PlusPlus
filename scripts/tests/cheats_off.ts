// SPDX-License-Identifier: GPL-3.0-or-later
//
// Cheats turned off in the options window, in a campaign park: a cheat's name only renames the guest, with no money
// and no message, and F12 shows the developer windows without the editor and the Debug tab. Allowed again, a cheat
// is used; the park window says so, and still does once the park is saved and loaded with cheats off.
//
//   ./nob script scripts/tests/cheats_off.ts -- --no-audio

export {};

await park.open("Campaigns/Base/Gunslinger.dat");
await park.ready();
await frames(30);
park.dropParties(30, 3);
await park.advance(5);
await frames(5);

const name = (key: string): string => must(ui.text(key), `the name under ${key}`);
const guest = (): PersonIndex =>
    must(
        people.list().find((p) => !p.staff && p.state === "walking"),
        "a guest on foot",
    ).index;
const cheatMessages = (): number => park.messages().filter((m) => m.key.startsWith("Rct3pp_Cheat_")).length;

// The options window's General page: cheats are allowed until the player unticks them.
await files_menu("File_GameOptions");
await until(() => ui.find("options") !== undefined, 120, "the options window");
const box = "options/content/GameOptionsDialog_GameSettingsTabHelp/cheats";
expect(ui.find(box)?.on === true, "cheats allowed at first");
await ui.toggle(box, false);
expect(ui.find(box)?.on === false, "cheats off once unticked");
await frames(5);
await screenshot("options_cheats_off");
keys.press("Escape");
await until(() => ui.find("options") === undefined, 120, "the options window to close");
expect(!park.cheatsUsed(), "no cheat used yet");

// A cheat's name is only a name: no money, no message, nothing counted.
const cash = must(park.cash(), "a park with money");
const renamed = guest();
people.rename(renamed, name("CheatPeepName_Rockefeller"));
await frames(3);
log("cash", cash, park.cash());
expect(park.cash()! - cash < 1_000_000, "no cheat's money");
expect(people.list().find((p) => p.index === renamed)?.name === name("CheatPeepName_Rockefeller"), "the name taken");
people.rename(guest(), name("CheatPeepName_Unbreakable"));
await frames(3);
expect(cheatMessages() === 0, "no cheat's message");
expect(!park.cheatsUsed(), "no cheat used");

// The developer windows show, but neither the editor nor the Debug tab.
keys.press("F12");
await frames(5);
expect(!editor.shown(), "the editor shut");
keys.press("F12");
await frames(2);

// Allowed again, the money comes and the park counts a cheat used.
options.set("cheats", 1);
const before = park.cash()!;
people.rename(guest(), name("CheatPeepName_Rockefeller"));
await frames(3);
log("cash", before, park.cash());
expect(park.cash()! - before >= 1_000_000, "the cheat's money");
expect(park.cheatsUsed(), "a cheat used");

// Off again, saved and loaded: the park still says a cheat was used.
options.set("cheats", 0);
await files_menu("File_SaveGame");
await until(() => ui.find("park_files") !== undefined, 120, "the save window");
await ui.click("park_files/content/open");
await until(() => ui.find("park_files") === undefined, 120, "the save window to close");
await files_menu("File_LoadGame");
// Loading asks first whether to save; the park was saved just now.
await until(() => ui.find("load_question") !== undefined, 120, "the question before loading");
await ui.click("load_question/content/answer1");
const first = "park_files/content/list/view/content/row0";
await until(() => ui.find(first) !== undefined, 120, "the save in the load window");
await ui.click(first);
await ui.click("park_files/content/open");
await until(() => ui.find("park_files") === undefined && park.loaded(), 1200, "the saved park");
await park.ready();
await frames(30);
expect(park.cheatsUsed(), "a cheat used, after loading");

await ui.click(`tools/Management/${must(ui.children("tools/Management"), "tools/Management shown")[0]}`);
await until(() => ui.find("tools/Management/Man_Park") !== undefined, 120, "the management menu");
await ui.click("tools/Management/Man_Park");
await until(() => under("PID_Title", "Rct3pp_CheatsUsed_value") !== undefined, 120, "the park window's cheats line");
const said = ui.find(must(under("PID_Title", "Rct3pp_CheatsUsed_value"), "the cheats line"))?.text;
expect(said === name("Yes"), `the park window to say a cheat was used, not ${said}`);
await frames(10);
await screenshot("park_window_cheats_used");
quit(0);
