// SPDX-License-Identifier: GPL-3.0-or-later
//
// The animal lines of the finances window in a Wild! scenario: an animal bought into an enclosure is booked on
// "Animal purchase/sale" for this month, not on ride construction. Screenshot: the summary with the line.
//
//   ./nob script scripts/tests/zoo_finances.ts -- --no-intro --no-audio --seed 1

export {};

const kWindow = "FinancialManagement_Title";

/** This month's cell of the summary's row labelled with string `key`, undefined when the row is not shown. */
function this_month(key: string): string | undefined {
    const table = must(ui.find(must(under(kWindow, "table"), "the summary's table")), "the table shown");
    const label = must(ui.text(key), `the text of ${key}`);
    return table.rows?.find((cells) => cells[0] === label)?.[1];
}

async function open_summary(): Promise<void> {
    if (ui.find("tools/Management/Man_Finances") === undefined)
        await ui.click(`tools/Management/${must(ui.children("tools/Management"), "tools/Management shown")[0]}`);
    await until(() => ui.find("tools/Management/Man_Finances") !== undefined, 120, "the management menu");
    await ui.click("tools/Management/Man_Finances");
    await until(() => under(kWindow, "FinancialSummaryTabIcon") !== undefined, 120, "the finances window");
    await ui.click(must(under(kWindow, "FinancialSummaryTabIcon"), "the summary tab"));
    await frames(10);
}

await park.open("Campaigns/Base/Wild/ScrubGardens.dat");
await park.ready();
await frames(10);

await open_summary();
const before = this_month("Money_AnimalBuyingSelling");
log("animal purchase/sale before", before);
expect(before !== undefined, "the animal purchase/sale line in a Wild! park");
for (const key of ["Money_AnimalFood", "Money_AnimalAdoptions", "Money_AnimalCosts"])
    expect(this_month(key) !== undefined, `the ${key} line`);
const construction = this_month("Money_RideConstruction");
await ui.click(must(under(kWindow, "close"), "the close button"));
await until(() => ui.find(kWindow) === undefined, 120, "the finances window to close");

// One of the park's own kind, put down where one of its animals stands.
const home = must(
    animals.list().find((a) => a.enclosure >= 0),
    "an animal in an enclosure",
);
const col = Math.floor(home.at.col);
const row = Math.floor(home.at.row);
await look_down(col + 0.5, row + 0.5);
const cash = must(park.cash(), "a park with money");
const had = animals.list().length;
expect(build.pick("WildAnimals/WildAnimals", home.species), `the ${home.species} in the animal list`);
await pointer.click(...on_view(col + 0.5, row + 0.5));
await frames(3);
build.stop();
expect(animals.list().length === had + 1, "the animal bought");
const paid = cash - must(park.cash(), "a park with money");
log("paid", paid);
expect(paid > 0, "the animal paid for");

await open_summary();
const after = must(this_month("Money_AnimalBuyingSelling"), "the animal purchase/sale line");
log("animal purchase/sale after", after, "ride construction", construction, "to", this_month("Money_RideConstruction"));
expect(after !== before, `the purchase on the animal line, still ${after}`);
expect(this_month("Money_RideConstruction") === construction, "nothing booked on ride construction");
await screenshot("summary");

quit();
