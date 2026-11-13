// SPDX-License-Identifier: GPL-3.0-or-later
//
// The construction rights prize on a scenario that holds land back for it: before it the land is not for sale,
// after it the map shows it on sale and the buying tool buys it like any other.
//
//   ./nob script scripts/tests/prize_land.ts -- --no-audio

export {};

/** The path of the widget named `name` somewhere under `path`. */

/** Clicks a tool's own button, which comes first in its group. */

const kConstructionRights = 7;

await park.open("Campaigns/Base/NewBlood.dat");
await park.ready();
await until(() => ui.find("tools/Management") !== undefined, 1200, "the tool row");
await frames(30);

// The held back tiles, and of them the one nearest land the park owns.
const { width, height } = map.size();
const held: [number, number][] = [];
const owned: [number, number][] = [];
for (let col = 0; col < width; ++col)
    for (let row = 0; row < height; ++row) {
        const kind = map.tile(col, row)!.ownership;
        if (kind === "locked")
            held.push([col, row]);
        else if (kind === "owned")
            owned.push([col, row]);
    }
log("held back", held.length, "owned", owned.length);
expect(held.length > 0, "land held back");
const distance = (t: [number, number]) => Math.min(...owned.map(o => Math.abs(o[0] - t[0]) + Math.abs(o[1] - t[1])));
const [col, row] = held.reduce((best, t) => (distance(t) < distance(best) ? t : best));

await click_tool("Map");
await frames(30);
await screenshot("map_before");
await click_tool("Map");

park.givePrize(kConstructionRights);
await until(() => ui.find("prize_won") !== undefined, 120, "the prize box");
await screenshot("prize_box");
await ui.click(`prize_won/${must(ui.children("prize_won"), "prize_won shown").at(-1)!}`);
await until(() => ui.find("prize_won") === undefined, 120, "the box to close");
expect(held.every(([c, r]) => map.tile(c, r)!.ownership === "for sale"), "the held back land on sale");

await click_tool("Map");
await frames(30);
await screenshot("map_after");
await click_tool("Map");

// Bought with the park window's buying tool.
await click_tool("Management");
await until(() => ui.find("tools/Management/Man_Park") !== undefined, 120, "the management menu");
await ui.click("tools/Management/Man_Park");
await until(() => under("PID_Title", "ParkInfoIcon_BuyLand") !== undefined, 120, "the park window's buy land button");
await ui.click(must(under("PID_Title", "ParkInfoIcon_BuyLand"), "ParkInfoIcon_BuyLand under PID_Title"));
await frames(10);
// The window out of the way; the tool stays out.
await ui.click(must(under("PID_Title", "close"), "close under PID_Title"));
await frames(10);
camera.look({ col: col + 0.5, row: row + 0.5 });
await frames(30);
const at = map.screen(col, row);
expect(at !== undefined, "the tile in sight");
const cash = park.cash();
await pointer.click(at!.x, at!.y);
await frames(30);
await screenshot("bought");
const bought = held.filter(([c, r]) => map.tile(c, r)!.ownership === "owned").length;
log("bought", bought, "cash", cash, "->", park.cash());
expect(bought > 0, "land that was held back bought"); // Under the brush round the pointer, which a wall may move
expect(cash === undefined || park.cash()! < cash, "the land paid for");
quit(0);
