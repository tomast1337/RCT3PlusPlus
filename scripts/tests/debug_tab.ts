// SPDX-License-Identifier: GPL-3.0-or-later
//
// The developer windows' Debug tab on a busy park: shown beside the park, then its tools used one after another
// and their effects checked: money added, guests added and sent home, every ride broken down and reset, litter
// dropped and cleared, research finished, a stall item's name reset. Keeps a screenshot of the tab and one with the tools used.
//
//   ./nob script scripts/tests/debug_tab.ts -- --no-audio --park "<a busy park's save>"
//
// Without --park it opens a scenario of the game's, where the guests are the ones the tab adds.

export {};

for (let frame = 0; frame < 3600 && !park.loaded(); ++frame) await frames(1);
if (!park.loaded()) await park.open("Campaigns/Base/VanillaHills.dat", { sandbox: false });
await park.ready();
await frames(30);

// A stall item renamed in its menu, then every item's name back to its kind's.
const stall = stalls.list().find((st) => st.items.length > 0 && !st.demolished)?.index;
if (stall !== undefined) {
    stalls.show(stall);
    await until(() => ui.find("info/Stall_ItemsIcon") !== undefined, 120, "the stall's window");
    await ui.click("info/Stall_ItemsIcon");
    await frames(2);
    const nameBox = "info/body/StallMenu/menu/view/content/ItemName0";
    await until(() => ui.find(nameBox) !== undefined, 60, "the item's name box");
    await ui.click(nameBox);
    keys.press("Ctrl+A");
    keys.type("Debug Treat");
    keys.press("Enter");
    await frames(3);
    expect(
        stalls.list()[stall].items[0].name === "Debug Treat",
        `a stall item renamed, not "${stalls.list()[stall].items[0].name}"`,
    );
    expect(debug.use("reset_item_names") >= 1, "the item's name reset");
    expect(
        stalls.list().every((st) => st.items.every((i) => i.name === "%ME%")),
        "every sold item named after its kind",
    );
    expect(debug.use("reset_item_names") === 0, "nothing more to reset");
    await ui.click("info/close");
    await frames(2);
} else log("no stall to rename an item in");

keys.press("F12");
await frames(2);
debug.open();
await frames(10);
await screenshot("debug_tab");

// Money: what is asked for, at once.
const cash = park.cash();
if (cash !== undefined) {
    expect(debug.use("add_money", 1_000_000) === 1, "money added");
    log("cash", cash, "to", park.cash());
    expect(park.cash() === cash + 1_000_000, "the cash up by what was added");
}

// Guests: as many as asked for, or a party more.
const before = park.guests();
const added = debug.use("add_guests", 200);
log("guests", before, "added", added, "now", park.guests());
expect(added >= 200 && park.guests() >= before + 200, "the guests added");

// Rides (facilities are run as shops): every ride the park runs broken down, then repaired.
const joined = () => rides.list().filter((r) => r.joined === true && r.kind !== "facility");
expect(joined().length > 0, "rides the park runs");
const broke = debug.use("break_rides");
log("rides", joined().length, "broke", broke);
expect(
    joined().every((r) => r.broken === true && r.open === false),
    "every ride broken down",
);

// Litter dropped over the paths, seen by the simulation.
const dropped = debug.use("drop_litter", 300);
expect(dropped === 300 && park.litter().length >= 300, "the litter dropped");
await park.advance(5);
await frames(20);
await screenshot("tools_used");

expect(debug.use("clear_litter") >= 300 && park.litter().length === 0, "the litter cleared");
debug.use("reset_rides");
expect(
    joined().every((r) => r.broken === false && r.queue === 0),
    "every ride repaired, its queue empty",
);

// Research: nothing left to invent, where the park researches at all.
const invented = debug.use("finish_research");
log("invented", invented, "left", park.research().left);
expect(park.research().left === null || park.research().left === 0, "research finished");

// Everyone home, and the park runs on.
const went = debug.use("remove_guests");
log("sent home", went);
expect(went >= added && park.guests() === 0, "the guests gone");
await park.advance(30);
await frames(10);
log("guests after", park.guests());
quit(0);
