// SPDX-License-Identifier: GPL-3.0-or-later
//
// A still right click deletes what is under the pointer while a tool that builds or places things one by one is out,
// and the tool stays out: a pool square and a pool path with the pool tools, refunded as the pool delete tool refunds
// them; a tree with the tree tool or a ride being placed, and a path with the path panel's controls. A right drag
// turns the camera and deletes nothing. Escape puts each tool away.
//
//   ./nob script scripts/tests/right_click_delete.ts -- --no-intro --no-audio --no-simulate --seed 1

export {};

const kPool = "Pool/Default/Default_Stub";

await park.open("Campaigns/Base/VanillaHills.dat");
await until(() => ui.find("tools/Land/Land") !== undefined && build.flatRides() !== undefined, 1200, "the build lists");

// Six by six of the park's own land, near flat, with nothing on it.
const { width, height } = map.size();
const taken_by_objects = new Set(map.objects().map((o) => `${o.col},${o.row}`));
const taken = new Set([...taken_by_objects, ...map.paths().map((o) => `${o.col},${o.row}`)]);
const clear = (c: number, r: number) => {
    const level = map.tile(c, r)?.corners[0] ?? 0;
    for (let row = r - 1; row < r + 7; ++row)
        for (let col = c - 1; col < c + 7; ++col) {
            const tile = map.tile(col, row);
            if (!tile?.owned || tile.water !== undefined || tile.corners.some((h) => Math.abs(h - level) > 1) || taken.has(`${col},${row}`))
                return false;
        }
    return true;
};
let site: { col: number; row: number } | undefined;
for (let r = 1; r < height - 8 && !site; r += 2)
    for (let c = 1; c < width - 8 && !site; c += 2)
        if (clear(c, r))
            site = { col: c, row: r };
expect(site !== undefined, "room for a pool");
const c0 = site!.col;
const r0 = site!.row;
const at_tile = (col: number, row: number) => map.objects().filter((o) => o.overlay === kPool && o.col === col && o.row === row);
camera.look({ col: c0 + 3, row: r0 + 3 }, { col: c0 + 3, row: r0 + 11, up: 9 });
await frames(10);

// Paving three by three, then a right click on its middle square with the tool still out.
expect(build.pick(kPool, "Pool_PlaceTiles"), "the pool tiles");
await frames(2);
await pointer.drag(on_view(c0 + 0.5, r0 + 0.5), on_view(c0 + 2.5, r0 + 2.5), 6);
await frames(3);
expect(at_tile(c0 + 1, r0 + 1).some((o) => o.symbol === "SolidTop"), "the paving laid");
await screenshot("right_click_pool_before");
let cash = park.cash()!;
await pointer.click(...on_view(c0 + 1.5, r0 + 1.5), "right");
await frames(3);
const square_refund = park.cash()! - cash;
// The tool shows the square it would lay under the pointer again; away from the site it is gone.
pointer.move(...on_view(c0 + 5.5, r0 + 5.5));
log("pool square refund", square_refund);
await until(() => at_tile(c0 + 1, r0 + 1).length === 0 && at_tile(c0 + 5, r0 + 5).length > 0, 300, "the square taken up, and the tool still out showing its square away from it");
expect(square_refund > 0, `the square refunded, not ${square_refund}`);
await screenshot("right_click_pool_after");

// A pool path of three along a row below, the middle piece right clicked.
expect(build.pick(kPool, "PoolPath"), "the pool path");
await frames(2);
await pointer.drag(on_view(c0 + 0.5, r0 + 4.5), on_view(c0 + 2.5, r0 + 4.5), 6);
await frames(3);
expect(at_tile(c0 + 1, r0 + 4).some((o) => o.symbol.startsWith("Path")), "the pool path laid");
cash = park.cash()!;
await pointer.click(...on_view(c0 + 1.5, r0 + 4.5), "right");
await frames(3);
const path_refund = park.cash()! - cash;
build.stop();
log("pool path refund", path_refund);
await until(() => at_tile(c0 + 1, r0 + 4).length === 0 && at_tile(c0, r0 + 4).length > 0, 300, "the middle pool path taken up, its neighbours left");
expect(path_refund > 0, `the pool path refunded, not ${path_refund}`);

// A tree on the grass beside the pool, then right clicked with the tree tool still out.
const tree = { col: c0 + 5, row: r0 + 1 };
const trees = () => map.objects().filter((o) => o.symbol === "Common_Linden_2" && o.col === tree.col && o.row === tree.row).length;
expect(build.pick("Style/Vanilla/Style", "Common_Linden_2"), "a tree in the scenery lists");
await frames(2);
pointer.move(...on_view(tree.col + 0.5, tree.row + 0.5));
await frames(3);
await pointer.click(...on_view(tree.col + 0.5, tree.row + 0.5));
await frames(3);
expect(trees() === 1, `a tree planted, not ${trees()}`);

// A right drag turns the camera and leaves the tree and the tool.
const view = camera.view();
const [x, y] = on_view(tree.col + 0.5, tree.row + 0.5);
await pointer.drag([x, y - 0.03], [x + 0.08, y - 0.03], 10, "right");
await frames(3);
const turned = camera.view();
log("right drag", JSON.stringify(view), JSON.stringify(turned));
expect(turned.yaw !== view.yaw || turned.target.some((v, i) => v !== view.target[i]), "the camera moved by the right drag");
expect(trees() === 1 && build.placing() !== undefined, "the tree and the tool left by the right drag");
camera.look({ col: c0 + 3, row: r0 + 3 }, { col: c0 + 3, row: r0 + 11, up: 9 });
await frames(5);
await screenshot("right_click_tree_before");
cash = park.cash()!;
const [tx, ty] = on_view(tree.col + 0.5, tree.row + 0.5);
await pointer.click(tx, ty - 0.03, "right");
await frames(3);
log("tree removal", park.cash()! - cash);
expect(trees() === 0, "the tree deleted by a right click");
expect(build.placing() !== undefined, "the tree tool still out");
await screenshot("right_click_tree_after");
keys.press("Escape");
await frames(2);
expect(build.placing() === undefined, "the tree tool put away by Escape");

// With a ride being placed, a right click deletes the tree pointed at and the ride's ghost stays; Escape puts it away.
const plant = async () => {
    expect(build.pick("Style/Vanilla/Style", "Common_Linden_2"), "a tree in the scenery lists");
    await frames(2);
    pointer.move(tx, ty);
    await frames(3);
    await pointer.click(tx, ty);
    await frames(3);
    keys.press("Escape");
    await frames(2);
    expect(trees() === 1, `a tree planted again, not ${trees()}`);
};
await plant();
const ride = build.flatRides()![0];
expect(build.pick(ride.overlay, ride.symbol), `${ride.symbol} picked`);
await frames(2);
await pointer.click(tx, ty - 0.03, "right");
await frames(3);
expect(trees() === 0, "the tree deleted by a right click while placing a ride");
expect(build.placing()?.step === "ride", "the ride still being placed");
keys.press("Escape");
await frames(2);
expect(build.placing() === undefined, "the ride's placing put away by Escape");

// Escape puts every other tool away too: the pool tool, the land tools, both delete tools, the fence tool and the
// structure picking out tool. A tool menu left open after picking from it closes on the first Escape.
const escape = async (item?: string) => {
    if (item && ui.find(item)) {
        keys.press("Escape");
        await frames(2);
        expect(ui.find(item) === undefined, `the menu of ${item} closed by Escape first`);
    }
    keys.press("Escape");
    await frames(2);
};
if (!ui.find("tools/Rides/Obj_Pools"))
    await ui.click(`tools/Rides/${must(ui.children("tools/Rides"), "tools/Rides shown")[0]}`);
await ui.click("tools/Rides/Obj_Pools");
await until(() => ui.find("pool_panel") !== undefined, 120, "the pool panel");
expect(build.pick(kPool, "Pool_PlaceTiles"), "the pool tiles");
await frames(2);
await escape("tools/Rides/Obj_Pools");
expect(ui.find("pool_panel") === undefined, "the pool tool put away by Escape");

if (!ui.find("tools/Land/Land_Brush_Height"))
    await ui.click("tools/Land/Land");
await ui.click("tools/Land/Land_Brush_Height");
await frames(2);
expect(ui.find("land_panel") !== undefined, "the shaping tool's panel");
await escape("tools/Land/Land_Brush_Height");
expect(ui.find("land_panel") === undefined, "the shaping tool put away by Escape");

await plant();
if (!ui.find("tools/Delete/GUI2_DeleteSelection"))
    await ui.click("tools/Delete/Obj_Delete");
await ui.click("tools/Delete/GUI2_DeleteSelection");
await frames(2);
expect(ui.find("delete_area") !== undefined, "the area delete panel");
await pointer.click(tx, ty - 0.03, "right");
await frames(3);
expect(trees() === 0, "the tree deleted by a right click with the area delete tool");
expect(ui.find("delete_area") !== undefined, "the area delete tool still out after the right click");
await escape("tools/Delete/GUI2_DeleteSelection");
expect(ui.find("delete_area") === undefined, "the area delete tool put away by Escape");

await plant();
if (!ui.find("tools/Delete/GUI2_DeleteSingleItem"))
    await ui.click("tools/Delete/Obj_Delete");
await ui.click("tools/Delete/GUI2_DeleteSingleItem");
await frames(2);
await escape("tools/Delete/GUI2_DeleteSingleItem");
await pointer.click(tx, ty - 0.03);
await frames(3);
expect(trees() === 1, "the tree left by a click once Escape put the delete tool away");
await ui.click("info/close").catch(() => undefined);

expect(build.pick("Enclosures/Fences/Wood/Wood_Stub", "Wood"), "the wooden fence in the enclosure list");
await frames(2);
keys.press("Escape");
await frames(2);
const [fx, fy] = on_view(c0 + 0.5, r0 + 5.5);
pointer.move(fx, fy);
await frames(2);
pointer.press();
pointer.move(...on_view(c0 + 2.5, r0 + 5.5));
await frames(3);
const fencing = build.enclosing();
pointer.release();
await frames(2);
expect(fencing === undefined, "no fence drawn once Escape put the fence tool away");

await plant();
await ui.click("tools/Scenery/Obj_Scenery");
await ui.click("tools/Scenery/SavedSelection_SubMenu");
await ui.click("tools/Scenery/SavedSelection_Grab");
await frames(2);
expect(ui.find("structure_grab") !== undefined, "the picking out panel");
await pointer.click(tx, ty - 0.03, "right");
await frames(3);
expect(trees() === 0, "the tree deleted by a right click with the picking out tool");
expect(ui.find("structure_grab") !== undefined, "the picking out tool still out after the right click");
await escape("tools/Scenery/SavedSelection_Grab");
expect(ui.find("structure_grab") === undefined, "the picking out tool put away by Escape");

// A level path of the park, in sight.
const paths = map.paths().filter((p) => !p.raised && !p.queue && !taken_by_objects.has(`${p.col},${p.row}`));
const road = paths.find((p) => paths.some((q) => q.col === p.col + 1 && q.row === p.row) && paths.some((q) => q.col === p.col - 1 && q.row === p.row));
expect(road !== undefined, "a level path with paths beside it");
camera.look({ col: road!.col + 0.5, row: road!.row + 0.5 }, { col: road!.col + 0.5, row: road!.row + 8.5, up: 25 });
await frames(10);

// The path panel's controls out, a right click sells the path pointed at.
await ui.click("tools/Paths/Obj_Paths");
await until(() => ui.find("footpaths/route") !== undefined, 120, "the path panel");
await ui.toggle("footpaths/follow", false);
cash = park.cash()!;
await pointer.click(...on_view(road!.col + 0.5, road!.row + 0.5), "right");
await frames(5);
log("path refund", park.cash()! - cash);
expect(!map.paths().some((p) => p.col === road!.col && p.row === road!.row), "the path sold by a right click");
expect(park.cash()! > cash, "the path refunded");
expect(ui.find("footpaths/route") !== undefined, "the path panel still out");
quit(0);
