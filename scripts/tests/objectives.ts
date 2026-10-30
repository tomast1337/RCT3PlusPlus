// SPDX-License-Identifier: GPL-3.0-or-later
//
// A level whose only goal is a number of guests. The objectives window shows where the park stands on it; with
// enough guests brought in, the next day's check wins the level: its prize is told in a box, and the window then
// marks the goal and the level completed.
//
//   ./nob script scripts/tests/objectives.ts -- --no-audio

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

const kGuestsWanted = 200; // The level's goal, as the park file sets it
const kDay = 90 / 7;

await park.open("Campaigns/Base/NewBlood.dat");
await park.ready();
await until(() => ui.find("tools/Management") !== undefined, 1200, "the tool row");
await frames(30);
expect(park.guests() < kGuestsWanted, "the level not met as it opens");

if (ui.find("tools/Management/Man_Objectives") === undefined)
    await ui.click(`tools/Management/${ui.children("tools/Management")![0]}`);
await until(() => ui.find("tools/Management/Man_Objectives") !== undefined, 120, "the management menu");
await ui.click("tools/Management/Man_Objectives");
await until(() => under("", "Objectives_DialogTitle") !== undefined, 120, "the objectives window");
park.setDate(park.date() + kDay);
await frames(30);
await screenshot("objectives_before");

for (let seed = 1; seed <= 100 && park.guests() < kGuestsWanted; ++seed)
    park.dropParties(20, seed);
log("guests", park.guests());
expect(park.guests() >= kGuestsWanted, "enough guests brought in");

park.setDate(park.date() + kDay);
await until(() => ui.find("prize_won") !== undefined, 600, "the prize box");
await screenshot("objectives_prize");
const box = ui.find("prize_won");
log("prize", JSON.stringify(box));
await ui.click(`prize_won/${ui.children("prize_won")!.at(-1)!}`);
await until(() => ui.find("prize_won") === undefined || ui.find("prize_won")!.text !== box!.text, 120, "the next box");
await frames(30);
await screenshot("objectives_won");
quit(0);
