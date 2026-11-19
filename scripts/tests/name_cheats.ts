// SPDX-License-Identifier: GPL-3.0-or-later
//
// Guests renamed with the game's cheat names, each name read from the language files by its key: the park gets
// its money, the guests on the paths jump for joy, laugh or retch, the rules' cheats say they are on, and the
// groups split up.
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

// Groups split up.
expect(
    people.list().some((p) => p.state !== undefined && p.party !== undefined),
    "guests in groups",
);
people.rename(guest(), name("CheatPeepName_AllGroupsIndividuals"));
await frames(3);
expect(
    people.list().every((p) => p.state === undefined || p.party === undefined),
    "no one in a group",
);
expect(
    park.messages().some((m) => m.key === "Rct3pp_Cheat_Alone"),
    "the message saying so",
);
await park.advance(30);
expect(park.guests() > 0, "the park runs on");

// A name that is no cheat's does nothing.
const cash = park.cash();
people.rename(guest(), "Someone");
await frames(3);
expect(park.cash() === cash, "no money for an ordinary name");
