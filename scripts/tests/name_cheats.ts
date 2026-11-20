// SPDX-License-Identifier: GPL-3.0-or-later
//
// Guests renamed with the game's cheat names, each name read from the language files by its key: the park gets
// its money, the guests on the paths jump for joy, laugh or retch, then for a while look round nervously or
// take photos, and the rules' cheats say they are on;
// a name the game knows that changes nothing leaves the groups be.
//
//   ./nob script scripts/tests/name_cheats.ts -- --no-audio

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

// Money, booked as other income.
const before = must(park.cash(), "a park with money");
people.rename(guest(), name("CheatPeepName_Rockefeller"));
await frames(3);
log("cash", before, park.cash());
expect(park.cash()! - before === 1_000_000, "10,000.00 more");

// Every guest on the paths at once; the case of the letters does not matter.
for (const [key, gesture] of [
    ["CheatPeepName_Sawyer", "jumpjoy"],
    ["CheatPeepName_Empty", "laugh"],
    ["CheatPeepName_Sick", "vomit"],
] as const) {
    people.rename(guest(), name(key).toUpperCase());
    await frames(3);
    const making = people.list().filter((p) => !p.staff && p.state !== undefined && p.gesture === gesture).length;
    log(key, making);
    expect(making >= 5, `guests showing ${gesture}`);
    await park.advance(10);
}

// For a while guests now and then look round behind them, sure they are watched, or stop to take a photo.
for (const [key, gesture] of [
    ["CheatPeepName_Hitchcock", "turnlook"],
    ["CheatPeepName_Photostory", "photo"],
] as const) {
    people.rename(guest(), name(key));
    let most = 0;
    for (let i = 0; i < 40 && most < 2; i++) {
        await park.advance(0.5);
        await frames(1);
        const making = people
            .list()
            .filter((p): p is WalkedPerson => !p.staff && p.state !== undefined && p.gesture === gesture);
        if (making.length > most) {
            most = making.length;
            camera.look(making[0], { col: making[0].col + 2, row: making[0].row + 2, up: making[0].up + 4 });
        }
    }
    log(key, most);
    expect(most >= 2, `guests showing ${gesture}`);
    await frames(3);
    await screenshot(gesture);
    await park.advance(35);
}

// The rules' cheats say they are on.
for (const [key, said] of [
    ["CheatPeepName_Unbreakable", "Rct3pp_Cheat_Unbreakable"],
    ["CheatPeepName_RideAllRides", "Rct3pp_Cheat_RideAllFlat"],
    ["CheatPeepName_RideAllCoasters", "Rct3pp_Cheat_RideAllTracked"],
    ["CheatPeepName_BigExplosion", "Rct3pp_Cheat_BigBlasts"],
] as const) {
    people.rename(guest(), name(key));
    await frames(3);
    expect(
        park.messages().some((m) => m.key === said),
        `the message ${said}`,
    );
}

// A cheat's name the game knows but that changes nothing leaves the groups as they were.
const grouped = (): number => people.list().filter((p) => p.state !== undefined && p.party !== undefined).length;
const before_groups = grouped();
expect(before_groups > 0, "guests in groups");
people.rename(guest(), name("CheatPeepName_AllGroupsIndividuals"));
await frames(3);
expect(grouped() === before_groups, "the groups kept");

// A cheat rct3++ has no system for yet says so, and the guest keeps the name.
const lean = guest();
people.rename(lean, name("CheatPeepName_Lean"));
await frames(3);
expect(
    park.messages().some((m) => m.key === "Rct3pp_Cheat_NotYet"),
    "the not-yet message",
);
expect(people.list().find((p) => p.index === lean)?.name === name("CheatPeepName_Lean"), "the name kept");

// A name that is no cheat's does nothing.
const cash = park.cash();
people.rename(guest(), "Someone");
await frames(3);
expect(park.cash() === cash, "no money for an ordinary name");
