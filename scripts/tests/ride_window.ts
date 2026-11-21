// SPDX-License-Identifier: GPL-3.0-or-later
//
// The ride window's pages on real rides: a coaster, a flat ride and a stall of a campaign park run a while, each
// page opened, its lines logged and checked against the running park, and a screenshot of each.
//
//   ./nob script scripts/tests/ride_window.ts -- --no-audio

export {};

/** Every label and value shown on the open page, as "label = value" lines. */
function page_lines(): string[] {
    const out: string[] = [];
    for (const name of ui.children("info/body") ?? []) {
        if (!name.endsWith("_label")) continue;
        const key = name.slice(0, -"_label".length);
        const label = ui.find(`info/body/${name}`)?.text ?? "";
        const value = ui.find(`info/body/${key}_value`)?.text;
        out.push(value === undefined ? `${key}: ${label}` : `${key}: ${label} = ${value}`);
    }
    return out;
}

/** The value shown beside the line of `key`. */
function value(key: string): string {
    return must(ui.find(`info/body/${key}_value`)?.text, `the ${key} line`);
}

await park.open("Campaigns/Base/Gunslinger.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined, 1200, "the park");
// Run a while so the rides take riders.
await ui.click("bar/Top_SuperFastForward");
await frames(1800);
// Paused, so the pages and the park agree while they are read.
await ui.click("bar/Top_Pause");

const list = rides.list();
const coaster = must(
    list.find((r) => r.kind === "tracked" && !r.demolished && (r.customers ?? 0) > 0),
    "a coaster that took riders",
);
const flat = must(
    list.find((r) => r.kind === "flat" && !r.demolished),
    "a flat ride",
);
const stall = must(
    stalls.list().find((s) => !s.demolished),
    "a stall",
);
// A facility, which the park runs with no trains or goes: measured as it takes customers, as a stall is.
const facility = must(
    list.find((r) => r.kind === "facility" && !r.demolished && r.joined),
    "a facility on the paths",
);

async function every_tab(what: string): Promise<void> {
    for (const tab of ui.children("info") ?? []) {
        if (!tab.startsWith("Attraction_") && !tab.startsWith("Guest_") && !tab.startsWith("Stall_")) continue;
        if (tab === "Attraction_Construction" || tab.startsWith("Attraction_General")) continue;
        await ui.click(`info/${tab}`);
        await frames(10);
        log(`${what} ${tab}:\n  ${page_lines().join("\n  ")}`);
        await screenshot(`${what}_${tab}`);
    }
}

/** The digits of `text` as a number: hundredths of a sum of money, or a count. */
function digits(text: string): number {
    return Number(text.replace(/[^0-9-]/g, ""));
}

// The texts the pages are worded with, as the game has them.
const per_hour = must(ui.text("AttractionNumberArea_PerHour"), "the per hour text");
expect(per_hour.includes("#"), `an amount mark in "${per_hour}"`);
const hourly = per_hour.replace("#", "").trim();
const ages = ["ZeroYearsAgo", "OneYearAgo", "ManyYearsAgo"].map((k) => must(ui.text(k), k));
expect(
    ages.every((a) => a !== ""),
    "the age texts",
);

rides.show(coaster.index);
await until(() => ui.find("info") !== undefined, 120, "the coaster's window");
await every_tab("coaster");

// Finances: the projections an hour while it runs, and the total it took (the scenario starts at none).
const run = rides.list()[coaster.index];
const customers = must(run.customers, "the coaster's customers");
expect(customers > 0, "the coaster to have had riders");
await ui.click("info/Attraction_Finances");
await frames(10);
for (const key of ["Income", "ProjectedRunningCost", "Profit"])
    expect(value(`AttractionFinancesDialogLabel_${key}`).includes(hourly), `${key} an hour`);
const total = digits(value("AttractionFinancesDialogLabel_TotalProfit"));
const taken = rides.list()[coaster.index];
expect(
    total === must(taken.customers, "customers") * must(taken.price, "price"),
    `total profit ${total} from ${taken.customers} riders at ${taken.price}`,
);
// Customers: riders an hour, the total, its age and the guests whose favourite it is.
await ui.click("info/Attraction_Customers");
await frames(10);
expect(value("AttractionCustomersDialogLabel_Customers").includes(hourly), "customers an hour");
expect(
    digits(value("AttractionCustomersDialogLabel_TotalCustomers")) === rides.list()[coaster.index].customers,
    "the total customers",
);
const age = value("AttractionCustomersDialogLabel_AgeYears");
expect(
    ages.some((a) => (a.includes("#") ? age.includes(a.replace("#", "").trim()) : age === a)),
    `an age, not "${age}"`,
);
expect(/^[0-9]+$/.test(value("AttractionCustomersDialogLabel_FavouriteRide")), "a count of favourites");
await screenshot("coaster_customers_checked");
rides.show(flat.index);
await frames(10);
await every_tab("flat");
// A flat ride's and a stall's riders an hour, measured as the park takes them.
await ui.click("info/Attraction_Customers");
await frames(10);
expect(value("AttractionCustomersDialogLabel_Customers").includes(hourly), "the flat ride's customers an hour");
rides.show(facility.index);
await frames(10);
await ui.click("info/Attraction_Customers");
await frames(10);
log(`facility Attraction_Customers:\n  ${page_lines().join("\n  ")}`);
expect(value("AttractionCustomersDialogLabel_Customers").includes(hourly), "the facility's customers an hour");
await ui.click("info/Attraction_Finances");
await frames(10);
log(`facility Attraction_Finances:\n  ${page_lines().join("\n  ")}`);
expect(value("AttractionFinancesDialogLabel_Income").includes(hourly), "the facility's income an hour");
await screenshot("facility_Attraction_Finances");
stalls.show(stall.index);
await frames(10);
await every_tab("stall");
quit(0);
