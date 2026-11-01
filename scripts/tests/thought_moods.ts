// SPDX-License-Identifier: GPL-3.0-or-later
//
// Each thought has a face before it: grinning when glad, plain when neither, red when put out. Guests dropped in the
// game's Chequered Flag go round until one has a few thoughts; their window's thoughts page shows a face before
// each. Then three messages telling thoughts are posted, a glad one, a plain one and a complaint, and the message
// drawer shows each with its face.
//
//   ./nob script scripts/tests/thought_moods.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

await park.open("Campaigns/Base/ChequeredFlag.dat");
await park.ready();
await frames(30);
expect(park.dropParties(6, 3).length > 0, "parties dropped in the park");

const thoughtful = () => {
    const list = people.list();
    let best = -1;
    for (let i = 0; i < list.length; ++i)
        if (!list[i].staff && (list[i].thoughts?.length ?? 0) > (best < 0 ? 2 : list[best].thoughts!.length))
            best = i;
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
quit(0);
