// SPDX-License-Identifier: GPL-3.0-or-later
//
// An animal house's window in a Wild! scenario: a click on a house opens it on its details, naming the species
// that live in it; its feeding page shows the schedule the park kept and changes it, and calls a keeper, who
// walks to the house; its keepers and thoughts pages open, and a click on its keeper's hut opens the same
// window. As the park runs, each filling of the troughs is booked on Animal food. Screenshots: each page, the
// called keeper, and the finances with the food.
//
//   ./nob script scripts/tests/animal_house.ts -- --headless --no-intro --no-audio --seed 1

export {};

const kFinances = "FinancialManagement_Title";

/** This month's Animal food cell of the finances summary, which it opens and closes. */
async function animal_food(): Promise<string> {
    if (ui.find("tools/Management/Man_Finances") === undefined)
        await ui.click(`tools/Management/${must(ui.children("tools/Management"), "tools/Management shown")[0]}`);
    await until(() => ui.find("tools/Management/Man_Finances") !== undefined, 120, "the management menu");
    await ui.click("tools/Management/Man_Finances");
    await until(() => under(kFinances, "FinancialSummaryTabIcon") !== undefined, 120, "the finances window");
    await ui.click(must(under(kFinances, "FinancialSummaryTabIcon"), "the summary tab"));
    await frames(10);
    const table = must(ui.find(must(under(kFinances, "table"), "the summary's table")), "the table shown");
    const label = must(ui.text("Money_AnimalFood"), "the text of Money_AnimalFood");
    const cell = must(table.rows?.find((cells) => cells[0] === label)?.[1], "this month's Animal food");
    await screenshot("finances");
    await ui.click(must(under(kFinances, "close"), "the close button"));
    await until(() => ui.find(kFinances) === undefined, 120, "the finances window to close");
    return cell;
}

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
expect(
    under("info", "ASCFoodCostsLabel_label") !== undefined ||
        ui.children("info/body")?.some((n) => n.startsWith("ASCFoodCostsLabel")) === true,
    "what each filling costs",
);
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

// Called, a keeper of its enclosure sets off to fill its troughs, and walks there.
await ui.click(must(under("info", "Staff_JobIsAnimalKeeperIcon"), "the call-keeper button"));
await frames(2);
const sent = () =>
    animals
        .houses()
        .flatMap((h) => h.keepers)
        .find((k) => k.job === "filling" && k.target === house.id);
const keeper = must(sent(), "a keeper sent to the house");
const away = (k: { at: { col: number; row: number } }) => Math.hypot(k.at.col - house.at.col, k.at.row - house.at.row);
log("keeper", keeper.id, "sent from", away(keeper), "tiles away");
await ui.click("bar/Top_Pause");
await park.advanceUntil(
    () => {
        const now = animals
            .houses()
            .flatMap((h) => h.keepers)
            .find((k) => k.id === keeper.id);
        return now !== undefined && (now.job !== "filling" || away(now) < 1.5);
    },
    120,
    "the keeper at the troughs",
);
await ui.click("bar/Top_Pause");
const there = must(
    animals
        .houses()
        .flatMap((h) => h.keepers)
        .find((k) => k.id === keeper.id),
    "the keeper",
);
log("keeper now", there.job, away(there), "tiles away");
expect(away(there) < away(keeper), "the keeper walked to the house");
await screenshot("keeper_called");

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

// The keepers fill the troughs as the park runs, each filling booked on Animal food.
const food_before = await animal_food();
await ui.click("bar/Top_Pause");
await park.advance(400);
await ui.click("bar/Top_Pause");
const food_after = await animal_food();
log("animal food", food_before, "then", food_after);
expect(food_after !== food_before, "the troughs' food paid for");

quit();
