// SPDX-License-Identifier: GPL-3.0-or-later
//
// A ride that keeps its default name is named in messages as its window names it, by its kind and number. Run Down
// Zoo is saved with a broken ride of a default name; left broken, it brings the reminder that it is still out of
// order, and that message names the ride's kind, with no marker left in it.
//
//   ./nob script scripts/tests/ride_name_messages.ts -- --no-intro --no-audio --seed 1

export {};

await park.open("Campaigns/Base/Wild/RunDownZoo.dat");
await until(() => ui.find("tools/Land/Land") !== undefined, 300, "the tool row");

const still = () => park.messages().find((m) => m.key === "RideStillBrokenMessage");
await park.advanceUntil(() => still() !== undefined, 900, "the still-broken reminder");
await frames(5);
const message = still()!;
log("reminder about", message.name);
expect(message.name !== "" && !message.name.includes("%"), `the ride named by its kind, not "${message.name}"`);
expect(
    park.messages().every((m) => !m.name.includes("%")),
    "no message with a marker for a name",
);
await screenshot("still_broken_message");
