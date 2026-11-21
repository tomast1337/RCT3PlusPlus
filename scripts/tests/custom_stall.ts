// SPDX-License-Identifier: GPL-3.0-or-later
//
// A custom stall, a plain shop front of its own, sells as a kind of stall the player picks. Placed beside the
// road in from the entrance on the game's blank landscape, it sells nothing and will not open. Its items page
// offers every kind of stall the park has, in a list that fits on the screen; picking one puts that kind's
// goods on its menu at their prices, with the kind's goods to drag on. It then opens, the park runs it, and
// guests dropped on the paths buy from it.
//
//   ./nob script scripts/tests/custom_stall.ts -- --no-audio

export {};

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(
    () => ui.find("tools/Paths/Obj_Paths") !== undefined && build.stalls() !== undefined,
    1200,
    "the build lists",
);
const front = must(
    build.stalls()!.find((s) => s.symbol === "GenericStall"),
    "the custom stall to pick",
);
expect(front.goods.length === 0, `the custom stall selling nothing of its own (${JSON.stringify(front.goods)})`);
const paths = map.paths();
const gate = paths.reduce((a, b) => (b.row > a.row ? b : a));
const road = must(
    paths.find((p) => p.col === gate.col && p.row === gate.row - 4),
    "the road in from the entrance",
);
camera.look({ col: gate.col + 0.5, row: road.row + 0.5 }, { col: gate.col + 0.5, row: road.row + 11.5, up: 25 });
await frames(3);
expect(build.pick(front.overlay, front.symbol), "custom stall picked");
await frames(2);
// Turned a quarter from where it starts it faces west, to the road.
const spot = must(map.screen(road.col + 1, road.row), "the tile in sight");
pointer.move(spot.x, spot.y);
await frames(3);
for (let turn = 0; turn < 4 && build.placing()?.site?.turns !== 1; ++turn) {
    await ui.click("placing/IconAttractionEditorRotate");
    pointer.move(spot.x, spot.y);
    await frames(3);
}
expect(build.placing()?.problem === "none", `placeable beside the road, not ${build.placing()?.problem}`);
await pointer.click(spot.x, spot.y);
await frames(3);
const i = must(
    stalls.list().find((s) => s.symbol === "GenericStall:sta"),
    "the custom stall in the park",
).index;
expect(stalls.list()[i].items.length === 0, `a new custom stall selling nothing (${JSON.stringify(stalls.list()[i])})`);
expect(stalls.list()[i].sellsAs === "", "a new custom stall selling as no kind");
expect(stalls.setState(i, "open") === "CantOpenStallNoItems", "a custom stall with nothing to sell staying closed");

// The items page: the kind to sell as over the menu, none picked yet.
stalls.show(i);
await frames(2);
await ui.click("info/Stall_ItemsIcon");
const choice = "info/body/Stall_SelectType";
await until(() => ui.find(choice) !== undefined, 60, "the kind of stall to pick");
expect(
    ui.find(choice)!.text === ui.text("Stall_SelectType"),
    `no kind picked yet (${JSON.stringify(ui.find(choice))})`,
);
await frames(2);
await screenshot("custom_stall_unset");

// The list of kinds opens in columns that fit on the screen.
const kinds = build.stalls()!.filter((s) => s.goods.length > 0 && build.offers(s.symbol) !== false);
const list = "Stall_SelectType.menu"; // Opened on the desktop, over the windows
/** Opens the list of kinds and picks its row `row`, the first being "<Select stall type>". */
async function pick(row: number, shot?: string): Promise<void> {
    await ui.click(choice);
    await until(() => ui.find(list) !== undefined, 60, "the list of kinds");
    await frames(2);
    if (shot !== undefined) {
        const shown = ui.find(list)!;
        log("kinds", kinds.length, "list", JSON.stringify(shown));
        expect(
            shown.top >= 0 && shown.bottom <= 1 && shown.left >= 0 && shown.right <= 1,
            "the list of kinds on the screen",
        );
        await screenshot(shot);
    }
    keys.press("Home");
    for (let k = 0; k < row; ++k) keys.press("Down");
    keys.press("Enter");
    await frames(3);
}
/** Checks the stall sells its kind's goods at their prices, and gives the kind. */
function selling(): StallItem {
    const stall = stalls.list()[i];
    const kind = must(
        kinds.find((k) => `${k.symbol}:sta`.toLowerCase() === stall.sellsAs.toLowerCase()),
        `the kind it sells as, ${stall.sellsAs}, among the stalls`,
    );
    log("sells as", stall.sellsAs, JSON.stringify(stall.items));
    expect(
        JSON.stringify(stall.items.map((x) => [x.item.toLowerCase(), x.price])) ===
            JSON.stringify(kind.goods.map((g) => [`${g.item}:cid`.toLowerCase(), g.price])),
        `selling ${kind.symbol}'s goods at their prices (${JSON.stringify(stall.items)})`,
    );
    return kind;
}
await pick(1, "custom_stall_kinds");
const first = selling();
expect(ui.find(choice)?.text !== ui.text("Stall_SelectType"), "the kind shown as picked");
expect(ui.find("info/body/StallMenu/menu/view/content/ItemName0") !== undefined, "the kind's first item on the menu");
await screenshot("custom_stall_set");
// Picked again, another kind's goods take the place of the first's, till one the park's guests buy.
for (let row = 2; row < 12 && !stalls.list()[i].joined; ++row) {
    await pick(row);
    expect(selling().symbol !== first.symbol, "another kind picked");
}
expect(stalls.list()[i].joined, "the park running the custom stall once it sells food or drink");

expect(stalls.setState(i, "open") == null, "the custom stall opening once it sells something");
park.dropParties(60, 3);
const customers = () => {
    const s = stalls.list()[i];
    return s.joined ? s.customers : 0;
};
for (let t = 0; t < 30 && customers() === 0; ++t) await park.advance(120);
log("customers", customers(), "guests", park.guests());
expect(customers() > 0, `guests buying at the custom stall (${JSON.stringify(stalls.list()[i])})`);
quit(0);
