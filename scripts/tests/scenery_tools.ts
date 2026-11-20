// SPDX-License-Identifier: GPL-3.0-or-later
//
// Works a tree over as a player in a park with money would, on a park of the campaign, the simulation stopped so
// only the tools move its money:
// - its ghost shows where it fits and is refused where one stands, and placing it costs its price;
// - its window's move tab picks it up again, turned and raised on the way, and sets it down elsewhere for nothing;
// - its window's copy tab takes out the tree tool, in the same colours;
// - pulling it down pays what its item says that costs, and the park saved and loaded again keeps the moved tree where it went;
// - a drag places a run of trees, a tile apart, and the scatter brush sprinkles them round the pointer, each paid for;
// - the undo key takes a whole stroke back, refunded.
//
//   RCT3PP_DATA=<an empty folder> ./nob script scripts/tests/scenery_tools.ts -- --no-audio --no-simulate

export {};

const kTree = "Common_Linden_2";

await park.open("Campaigns/Base/VanillaHills.dat", { sandbox: false });
await until(() => ui.find("tools/Land/Land") !== undefined && build.flatRides() !== undefined, 1200, "the build lists");
expect(park.cash() !== undefined, "a park with money");
park.setCash(1_000_000);

// Four by five of the park's own flat land, with nothing on it.
const { width, height: depth } = map.size();
const taken = new Set([...map.objects(), ...map.paths()].map((o) => `${o.col},${o.row}`));
const clear = (c: number, r: number) => {
    const level = map.tile(c, r)?.corners[0] ?? 0;
    for (let row = r - 1; row < r + 5; ++row)
        for (let col = c - 1; col < c + 4; ++col) {
            const tile = map.tile(col, row);
            if (
                !tile?.owned ||
                tile.water !== undefined ||
                tile.corners.some((h) => h !== level) ||
                taken.has(`${col},${row}`)
            )
                return false;
        }
    return true;
};
let site: { col: number; row: number } | undefined;
for (let r = 1; r < depth - 6 && !site; r += 2)
    for (let c = 1; c < width - 5 && !site; c += 2) if (clear(c, r)) site = { col: c, row: r };
expect(site !== undefined, "room for the trees");
const col = site!.col;
const row = site!.row;
camera.look({ col: col + 1.5, row: row + 1.5 }, { col: col + 1.5, row: row + 10.5, up: 12 });
await frames(10);

const trees = () =>
    map
        .objects()
        .filter(
            (o) =>
                o.symbol === kTree && o.col >= 0 && o.row >= 0 && o.col < map.size().width && o.row < map.size().height,
        );
const tree_at = (c: number, r: number) => trees().find((o) => o.col === c && o.row === r);

type Placing = NonNullable<ReturnType<typeof build.placing>>;
async function hover(c: number, r: number): Promise<{ x: number; y: number; placing: Placing }> {
    const [x, y] = on_view(c, r);
    pointer.move(x, y);
    await frames(3);
    const placing = build.placing();
    expect(placing !== undefined, "something placing");
    return { x, y, placing: placing! };
}

// 1. Placed for its price, its ghost refused where it then stands.
expect(build.pick("Style/Vanilla/Style", kTree), "a tree in the scenery lists");
await frames(2);
const fits = await hover(col + 0.5, row + 0.5);
expect(fits.placing.problem === "none", `the tree placeable, not ${fits.placing.problem}`);
const price = fits.placing.price;
expect(price > 0, `the tree's price shown, not ${price}`);
await screenshot("ghost_fits");
const before_place = park.cash()!;
await pointer.click(fits.x, fits.y);
await frames(2);
expect(tree_at(col, row) !== undefined, "the tree placed");
expect(park.cash() === before_place - price, `the tree paid for: ${before_place} then ${park.cash()}, price ${price}`);
const refused = await hover(col + 0.5, row + 0.5);
expect(
    refused.placing.problem === "blocked",
    `a second tree refused where the first stands, not ${refused.placing.problem}`,
);
await screenshot("ghost_refused");
build.stop();
await frames(2);
const turn = tree_at(col, row)!.turn;
const height = tree_at(col, row)!.height ?? 0;

// 2. Picked up with its window's move tab, turned and raised, and set down two tiles on for nothing.
const open_window = async (r = row) => {
    const [x, y] = on_view(col + 0.5, r + 0.5);
    await pointer.click(x, y - 0.03);
    await until(() => ui.find("info/Staff_MovePincerLarge") !== undefined, 120, "the tree's window with its move tab");
};
await open_window();
await screenshot("window");
await ui.click("info/Staff_MovePincerLarge");
await frames(2);
expect(build.placing() !== undefined, "the tree picked up");
expect(ui.find("info") === undefined, "the window shut once the tree is picked up");
const moving = await hover(col + 2.5, row + 0.5);
expect(moving.placing.price === 0, `moving free, not ${moving.placing.price}`);
keys.press(".");
await frames(2);
pointer.shift(true);
await frames(2);
pointer.move(moving.x, moving.y - 0.05);
await frames(3);
const raised = build.placing()!;
log("moved ghost", raised.site?.turns, raised.site?.height, "from", turn, height, raised.problem);
expect(raised.problem === "none", `the moved tree placeable, not ${raised.problem}`);
expect((raised.site?.height ?? 0) > height, "the moved tree raised with Shift");
const before_move = park.cash()!;
await pointer.click(moving.x, moving.y - 0.05);
pointer.shift(false);
await frames(3);
expect(build.placing() === undefined, "placing over once the tree is set down");
expect(tree_at(col, row) === undefined, "the tree gone from where it stood");
const moved = tree_at(col + 2, row);
expect(moved !== undefined, "the tree where it was moved to");
expect(moved!.turn === (turn + 1) % 4, `the moved tree turned a quarter, not ${moved!.turn} from ${turn}`);
expect((moved!.height ?? 0) > height, "the moved tree standing higher");
expect(park.cash() === before_move, `moving cost nothing: ${before_move} then ${park.cash()}`);
await screenshot("moved");

// 3. Its window's copy tab takes out the tree tool, priced again.
const [mx, my] = on_view(col + 2.5, row + 0.5);
await pointer.click(mx, my - 0.06);
await until(() => ui.find("info/Staff_MovePincerLarge") !== undefined, 120, "the moved tree's window");
const tabs = ui.children("info") ?? [];
const copy = tabs[tabs.indexOf("Staff_MovePincerLarge") + 1];
expect(copy !== undefined, "a copy tab after the move tab");
await ui.click(`info/${copy}`);
await frames(2);
const copying = await hover(col + 0.5, row + 2.5);
expect(copying.placing.price === price, `a copy at the tree's price, not ${copying.placing.price}`);
build.stop();
await frames(2);

// 4. Pulled down from its window, for what its item says pulling one down costs (a tree costs to clear).
await pointer.click(mx, my - 0.06);
await until(() => ui.find("info/Attraction_General_Delete") !== undefined, 120, "the moved tree's window again");
const before_delete = park.cash()!;
await ui.click("info/Attraction_General_Delete");
await frames(3);
const paid = before_delete - park.cash()!;
log("pulled down for", paid, "placed for", price);
expect(paid !== 0, "pulling the tree down paid for");
expect(tree_at(col + 2, row) === undefined, "the tree pulled down");
await ui.click("info/close").catch(() => undefined);
await screenshot("deleted");

// 5. A drag places a run of trees, one a tile, the ghost showing each and the price of all of them.
expect(build.pick("Style/Vanilla/Style", kTree), "the tree for a run");
await frames(2);
const start = await hover(col + 0.5, row + 1.5);
expect(start.placing.problem === "none", `the run's first tree placeable, not ${start.placing.problem}`);
pointer.press();
await frames(2);
const [ex, ey] = on_view(col + 2.5, row + 1.5);
pointer.move(ex, ey);
await frames(3);
const run = build.placing()!.run;
expect(run === 3, `a run of three trees shown, not ${run}`);
await screenshot("run_ghost");
const before_run = park.cash()!;
pointer.release();
await frames(3);
for (let c = col; c < col + 3; ++c) expect(tree_at(c, row + 1) !== undefined, `a tree of the run at ${c}`);
expect(park.cash() === before_run - 3 * price, `the run paid for: ${before_run} then ${park.cash()}`);
build.stop();
await frames(2);
await screenshot("run");

// 6. A tree of the run moved, saved and loaded again stays where it went.
await open_window(row + 1);
await ui.click("info/Staff_MovePincerLarge");
await frames(2);
const last = await hover(col + 0.5, row + 3.5);
await pointer.click(last.x, last.y);
await frames(3);
expect(tree_at(col, row + 3) !== undefined && tree_at(col, row + 1) === undefined, "the run's first tree moved");
await files_menu("File_SaveGame");
await until(() => ui.find("park_files") !== undefined, 120, "the save window");
await ui.click("park_files/content/open");
await until(() => ui.find("park_files") === undefined, 120, "the save window to close");
await files_menu("File_LoadGame");
await until(() => ui.find("load_question") !== undefined, 120, "the question before loading");
await ui.click("load_question/content/answer1");
const first = "park_files/content/list/view/content/row0";
await until(() => ui.find(first) !== undefined, 120, "the save in the load window");
await ui.click(first);
await ui.click("park_files/content/open");
await until(() => park.loaded() && tree_at(col, row + 3) !== undefined, 1200, "the saved park with the moved tree");
expect(tree_at(col, row + 1) === undefined, "nothing left where the moved tree first stood");
await screenshot("loaded");

// 7. With the scatter tab on, holding the button sprinkles trees round the pointer, each paid for, and a click
// places nothing more.
await until(() => build.flatRides() !== undefined, 1200, "the build lists again");
expect(build.pick("Style/Vanilla/Style", kTree), "the tree to scatter");
await until(() => ui.find("placing/Land_Brush_Height_Mode_Drag") !== undefined, 120, "the scatter tab");
await ui.click("placing/Land_Brush_Height_Mode_Drag");
await frames(2);
expect(build.placing()!.scatter === 1, `the brush scattering close together, not ${build.placing()!.scatter}`);
const count_before = trees().length;
const before_scatter = park.cash()!;
const [sx, sy] = on_view(col + 1.5, row + 2);
pointer.move(sx, sy);
await frames(2);
pointer.press();
await frames(30);
pointer.release();
await frames(3);
const scattered = trees().length - count_before;
log("scattered", scattered);
expect(scattered >= 2, `trees scattered, not ${scattered}`);
expect(
    park.cash() === before_scatter - scattered * price,
    `the scattered trees paid for: ${before_scatter} then ${park.cash()}`,
);
await screenshot("scattered");

// 8. The undo key takes the whole stroke back, refunded.
keys.press("Ctrl+Z");
await frames(3);
expect(trees().length === count_before, `the scattered trees taken back, ${trees().length} left of ${count_before}`);
expect(park.cash() === before_scatter, `the stroke refunded: ${before_scatter} then ${park.cash()}`);
await screenshot("undone");
build.stop();
await frames(2);
quit(0);
