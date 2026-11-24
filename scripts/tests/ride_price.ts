// SPDX-License-Identifier: GPL-3.0-or-later
//
// A ride's ticket price typed into its window's finances page is the price its guests pay from then on, and it is
// set while the park is paused as well as while it runs.
//
//   ./nob script scripts/tests/ride_price.ts -- --no-audio

export {};

await park.open("Campaigns/Base/VanillaHills.dat");
await park.ready();
await frames(30);
const ride = must(
    rides.list().find((r) => r.joined && r.price !== undefined),
    "a ride the guests use",
).index;
rides.show(ride);
await frames(10);
await ui.click("info/Attraction_Finances");
await frames(5);
const spinner = must(
    (ui.children("info/body") ?? []).find(
        (n) => n.startsWith("AttractionFinancesDialogLabel_AdmissionPrice") && !n.endsWith("_label"),
    ),
    `the price box, among ${JSON.stringify(ui.children("info/body"))}`,
);
const price = () => rides.list()[ride].price;

// Typed in and kept with Enter: hundredths, so 3.45 is 345.
async function type_price(text: string) {
    await ui.click(`info/body/${spinner}/field`);
    keys.press("Ctrl+A");
    keys.type(text);
    keys.press("Enter");
    await frames(3);
}

log("price at first", price());
await type_price("3.45");
log("price typed while running", price());
expect(price() === 345, `the guests' price 345, not ${price()}`);
await screenshot("ride_price");

keys.press("P");
await frames(2);
const tick = park.fingerprint().tick;
await type_price("1.20");
log("price typed while paused", price());
expect(park.fingerprint().tick === tick, "the park still paused");
expect(price() === 120, `the guests' price 120 while paused, not ${price()}`);
quit(0);
