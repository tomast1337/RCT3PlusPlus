// SPDX-License-Identifier: GPL-3.0-or-later
//
// An animal house's window in a Wild! scenario: a click on a house opens it on its details, naming the species
// that live in it; its feeding page shows the schedule the park kept and changes it; its keepers and thoughts
// pages open, and a click on its keeper's hut opens the same window. Screenshots: each page.
//
//   ./nob script scripts/tests/animal_house.ts -- --headless --no-intro --no-audio --seed 1

export {};

await park.open("Campaigns/Base/Wild/ScrubGardens.dat");
await park.ready();
await frames(30);

// Paused, so nothing walks between the view and the house.
await ui.click("bar/Top_Pause");
await frames(2);
const houses = animals.houses();
log("houses", JSON.stringify(houses));
const house = must(
    houses.find((h) => h.keepers.length > 0),
    "a house with a keeper",
);

// From above, a click on the house's middle opens its window on its details.
await look_down(house.at.col, house.at.row, 20);
await pointer.click(...on_view(house.at.col, house.at.row));
await until(() => animals.shownHouse() !== undefined, 120, "the house's window");
expect(animals.shownHouse()?.id === house.id, `the window on house ${house.id}, not ${animals.shownHouse()?.id}`);
expect(animals.shownHouse()?.tab === "details", `the details open, not ${animals.shownHouse()?.tab}`);
const species = must(ui.children("info/body"), "the details page").filter((n) => n.endsWith("_label"));
log("details", JSON.stringify(ui.children("info/body")));
expect(species.length > 0, "the species that live in it");
expect(under("info", "CenterInViewIcon") !== undefined, "the button that turns the view to it");
await screenshot("details");

// The feeding page: the park keeps its houses fed often; the list takes them down to infrequent.
await ui.click(`info/${must(info_tab("animalshelterfeedingicon"), "the feeding tab")}`);
await until(() => animals.shownHouse()?.tab === "feeding", 60, "the feeding page");
const schedule = () => animals.houses().find((h) => h.id === house.id)?.schedule;
expect(schedule() === "often", `the park's own schedule, not ${schedule()}`);
await screenshot("feeding");
const list = must(under("info", "AnimalShelterFeedingListLabel"), "the feeding list");
await ui.click(list);
await frames(2);
await ui.click(list);
await frames(2);
keys.press("Up");
await frames(2);
keys.press("Up");
await until(() => schedule() === "infrequent", 60, "the house fed infrequently");
await screenshot("feeding_infrequent");

for (const [part, page] of [
    ["animalshelterkeepericon", "keepers"],
    ["animalshelterpeepthoughtsicon", "thoughts"],
] as const) {
    await ui.click(`info/${must(info_tab(part), `the ${page} tab`)}`);
    await frames(5);
    expect(animals.shownHouse()?.tab === page, `the ${page} page open, not ${animals.shownHouse()?.tab}`);
    await screenshot(page);
}
expect(
    info_tab("attraction_general_delete") !== undefined && info_tab("animalshelterviewicon") !== undefined,
    "the delete and details tabs",
);

// The keeper's hut opens its house's window.
if (house.hut !== 0) {
    await ui.click("info/close");
    await until(() => animals.shownHouse() === undefined, 60, "the window closed");
    park.showObject(house.hut);
    await until(() => animals.shownHouse()?.id === house.id, 120, "the house's window from its hut");
}

quit();
