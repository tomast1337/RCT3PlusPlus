// SPDX-License-Identifier: GPL-3.0-or-later
//
// Each thought has a face before it: grinning when glad, plain when neither, red when put out. Guests dropped in the
// game's Chequered Flag go round until one has a few thoughts; their window's thoughts page shows a face before
// each. Then three messages telling thoughts are posted, a glad one, a plain one and a complaint, and the message
// drawer shows each with its face, and the guest list a face by how happy each guest is.
//
//   ./nob script scripts/tests/thought_moods.ts -- --no-audio

export {};

await park.open("Campaigns/Base/ChequeredFlag.dat");
await park.ready();
await frames(30);
expect(park.dropParties(6, 3).length > 0, "parties dropped in the park");

const thoughtful = () => {
    const list = people.list();
    let best = -1;
    for (let i = 0; i < list.length; ++i)
        if (!list[i].staff && (list[i].thoughts?.length ?? 0) > (best < 0 ? 2 : list[best].thoughts!.length)) best = i;
    return best;
};
await park.advanceUntil(() => thoughtful() >= 0, 600, "a guest with three thoughts");
const who = thoughtful();
log("thoughts", JSON.stringify(people.list()[who].thoughts));
people.show(who);
await until(() => ui.find("info/Guest_Thoughts") !== undefined, 120, "the guest's window");
await ui.click("info/Guest_Thoughts");
await frames(5);
// Lines with no key are named by their place.
const faces = (ui.children("info/body") ?? []).filter((c) => /^icon\d+$/.test(c));
log("faces", JSON.stringify(faces));
expect(faces.length >= 3, `a face before each thought, not ${faces.length}`);
await screenshot("guest_thoughts");
await ui.click("info/close");

// Glad, plain and put out, the newest at the bottom.
for (const key of ["Thought_ThisIsGoodValue1", "Thought_NeutralMusic1", "Thought_PI_Stall_TooExpensive1"])
    park.postMessage(key);
await frames(120);
expect(park.messages().length >= 3, "the messages posted");
await screenshot("message_faces");

// The guest list: each guest's row starts with the face of how happy they are, from the lowest third of the scale
// to the highest.
const bands = () => {
    const happiness = people.list().flatMap((p) => (p.staff || p.happiness === undefined ? [] : [p.happiness]));
    return [
        happiness.filter((h) => h < 1 / 3).length,
        happiness.filter((h) => h >= 1 / 3 && h <= 2 / 3).length,
        happiness.filter((h) => h > 2 / 3).length,
    ];
};
log("bands", JSON.stringify(bands()));
await park.advanceUntil(() => bands().every((n) => n > 0), 1800, "guests in each third of happiness");
log("bands", JSON.stringify(bands()));
await ui.click(`tools/Management/${must(ui.children("tools/Management"), "tools/Management shown")[0]}`);
await until(() => ui.find("tools/Management/Man_Guests") !== undefined, 120, "the management menu");
await ui.click("tools/Management/Man_Guests");
await until(() => ui.find("Man_GuestsHelp") !== undefined, 120, "the guest list");
await frames(5);
const tableOf = (path: string): string | undefined => {
    for (const child of ui.children(path) ?? []) {
        const at = `${path}/${child}`;
        if (child === "table") return at;
        const found = tableOf(at);
        if (found !== undefined) return found;
    }
    return undefined;
};
const table = must(ui.find(tableOf("Man_GuestsHelp")!), "the widget shown");
// Sorted by happiness one way and then the other, so both ends of the scale show. Of the table's 456 units across
// and 408 down, the happiness heading's middle is 124 from the right, left of the cash column and the scroll bar.
for (const shot of ["guest_list_least_happy", "guest_list_most_happy"]) {
    await pointer.click(
        table.right - ((table.right - table.left) * 124) / 456,
        table.top + ((table.bottom - table.top) * 10) / 408,
    );
    await frames(5);
    await screenshot(shot);
}
quit(0);
