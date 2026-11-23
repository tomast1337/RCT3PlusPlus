// SPDX-License-Identifier: GPL-3.0-or-later
//
// Selling zoo animals in a Wild! scenario whose goal is a profit from selling a species: the Sell button in an
// animal's window asks first, naming the price; a yes sells it, says so in a box, and books the money on the
// animal purchase/sale line. More sold through animals.sell() move the goal's reading up by what they fetched.
// Screenshots: the question, and the summary with the sales.
//
//   ./nob script scripts/tests/animal_sale.ts -- --no-intro --no-audio --seed 1

export {};

const kWindow = "FinancialManagement_Title";
const kGoal = "AnimalSalesProfitCondition";

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

/** The sales goal's reading, as the park reads it at the start of the next day. */
async function profit(): Promise<number> {
    park.setDate(park.date() + 90 / 7);
    await frames(10);
    log("date now", park.date(), JSON.stringify(park.goals().find((g) => g.kind === kGoal)));
    const goal = must(
        park.goals().find((g) => g.kind === kGoal),
        "the sales goal",
    );
    return must(goal.now, "the goal read");
}

await park.open("Campaigns/Base/Wild/OstrichFarm.dat", { sandbox: false });
await park.ready();
await frames(30);

const goal = must(
    park.goals().find((g) => g.kind === kGoal),
    "a goal on selling animals",
);
log("goal", JSON.stringify(goal));
const before = await profit();
const ours = () => animals.list().filter((a) => a.species.toLowerCase() === goal.species.toLowerCase() && !a.crated);
expect(ours().length >= 3, `animals of ${goal.species} to sell, ${ours().length}`);

// From the window: the question names what it fetches, and a yes sells it.
await open_summary();
const line = this_month("Money_AnimalBuyingSelling");
await ui.click(must(under(kWindow, "close"), "the close button"));
await until(() => ui.find(kWindow) === undefined, 120, "the finances window to close");
const first = must(ours()[0], "an animal to sell");
animals.open(first.id);
await until(() => animals.shown() !== undefined, 120, "the animal's window");
await frames(10);
await ui.click(must(under("info", "SellAnimal"), "the sell button"));
await until(() => ui.find("sell_animal_question") !== undefined, 120, "the question");
await frames(5);
const asked = must(ui.find("sell_animal_question/content/text")?.text, "the question's text");
log("asked", asked);
await screenshot("question");
const cash = must(park.cash(), "a park with money");
await ui.click("sell_animal_question/content/GUI2MiniDialog_Confirm");
await frames(5);
const got = must(park.cash(), "a park with money") - cash;
log("fetched", got, "for health", first.health, "species", first.species);
expect(got > 0, "money for the animal");
expect(!animals.list().some((a) => a.id === first.id), "the animal gone");
expect(animals.shown() === undefined, "its window closed");
expect(ui.find("prize_won") !== undefined, "a box saying it was sold");
await ui.click(`prize_won/${must(ui.children("prize_won"), "prize_won shown").at(-1)!}`);
const after_one = await profit();
log("goal reading", before, "to", after_one);
expect(after_one > before, `the goal up by the price, ${before} to ${after_one}`);

// More through the script call, each by what it fetched.
let sold = 0;
for (const a of ours().slice(0, 2)) {
    const price = must(animals.sell(a.id), `${a.name} sold`);
    expect(price > 0, `${a.name} fetched money`);
    sold += price;
}
await frames(5);
for (let i = 0; i < 2 && ui.find("prize_won") !== undefined; ++i) {
    await ui.click(`prize_won/${must(ui.children("prize_won"), "prize_won shown").at(-1)!}`);
    await frames(3);
}
const after = await profit();
log("goal reading", after_one, "to", after);
expect(after === after_one + sold, `the goal up by the sales, ${after_one} to ${after}`);

await open_summary();
const line_after = this_month("Money_AnimalBuyingSelling");
log("animal purchase/sale", line, "to", line_after);
expect(line_after !== line, "the sales on the animal purchase/sale line");
await screenshot("summary");

quit();
