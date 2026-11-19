// SPDX-License-Identifier: GPL-3.0-or-later
//
// A VIP's visit, walked: the first-level VIP of the game's Raiders of the Lost Coaster wants two exciting coasters, and
// the park has two. On the day they come in at the gate with their bodyguards, the arrival is told, and they walk off
// to the coaster they want most. Closing that coaster under them stops them, and they say so.
//
//   ./nob script scripts/tests/vip_route.ts -- --no-audio

export {};

const keys = () => park.messages().map((m) => m.key);

await park.open("Campaigns/Base/Wild/RaidersOfTheLostCoaster.dat");
await park.ready();
await frames(30);
const goal = must(
    park.vipGoals().find((g) => g.level === 0),
    "a VIP goal on the first level",
);
log("goal", JSON.stringify(goal));

park.setDate(goal.next);
await until(() => park.vips().length === 1, 120, "the VIP to come in");
expect(keys().at(-1) === "VIP_ArrivedInPark", "the arrival told");
await frames(20);
await screenshot("arrived");
const came = park.vips()[0];
log("vip", JSON.stringify(came));
expect(came.guards > 0, "bodyguards with the VIP");
expect(came.stops > 0, "a coaster the VIP wants to see");

await until(() => park.vips()[0].heading !== "", 600, "the VIP to set off for the coaster");
const heading = park.vips()[0].heading;
log("heading for", heading);
await until(() => park.vips()[0].shown !== null, 600, "the VIP to be dressed and shown");
await frames(900); // Down the path a way, the bodyguards beside them
const at = park.vips()[0];
camera.look({ col: at.col, row: at.row, up: 1 }, { col: at.col - 2, row: at.row + 2, up: 6 });
await frames(30);
await screenshot("vip_guards");

const ride = rides.list().findIndex((r) => r.name === heading);
expect(ride >= 0, `${heading} among the rides`);
const refused = rides.setState(ride as RideIndex, "closed");
expect(refused == null, `${heading} closed, not ${refused}`);
await until(() => keys().includes("VIP_AttractionClosed"), 600, "the VIP to say the coaster is closed");
expect(park.vips()[0].heading === "", "the VIP to stop walking to it");
await frames(10);
await screenshot("closed");
log("told", JSON.stringify(keys().slice(-4)));
quit(0);
