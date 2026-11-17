// SPDX-License-Identifier: GPL-3.0-or-later
//
// Saved designs waiting for a career scenario, then the Debug tab's tool that unlocks them. On a blank landscape
// with no career, the first kind of ride with a locked design: its window lists it locked, says why when it is
// picked, and does not build it. After the tool, the same window lists none locked and builds that design.
//
//   ./nob script scripts/tests/debug_designs.ts -- --no-audio
//
// Run with an empty data folder (RCT3PP_DATA), so no career has completed anything.

export {};

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(
    () => ui.find("tools/Paths/Obj_Paths") !== undefined && build.designs() !== undefined,
    1200,
    "the tools and saved designs",
);

/** Opens the designs window of the kind of `d`; whether it opened. */
async function openWindow(d: { symbol: string; overlay: string | null }): Promise<boolean> {
    if (!build.pick(d.overlay!, d.symbol, "designs")) return false;
    await until(() => ui.find("design_browser") !== undefined, 600, "the designs window");
    await frames(3);
    return true;
}

/** Clicks row `row` of the open designs window (0 the custom track's). */
async function clickRow(row: number): Promise<void> {
    const list = must(ui.find("design_browser/content/list"), "design_browser/content/list shown");
    const button = must(ui.find("design_browser/content/build"), "design_browser/content/build shown");
    const unit = (button.bottom - button.top) / 22; // A button is 22 of the window's units high
    await pointer.click(list.x, list.top + (18 + 16 * row + 8) * unit);
    await frames(3);
}

const saysWhy = () => (ui.children("design_browser/content/details/lines") ?? []).some((n) => n.includes("LockedHelp"));

async function close(): Promise<void> {
    await ui.click("design_browser/content/cancel");
    await until(() => ui.find("design_browser") === undefined, 120, "the designs window to close");
    build.stop();
    await frames(5);
}

// The first kind with a locked design, and that design's row.
const kinds = new Map<string, { symbol: string; overlay: string | null }>();
for (const d of build.designs()!) if (d.overlay) kinds.set(`${d.overlay}|${d.symbol}`, d);
let kind: { symbol: string; overlay: string | null } | undefined;
let lockedRow = 0;
for (const k of kinds.values()) {
    if (!(await openWindow(k))) continue;
    if (build.construction()!.locked > 0) {
        for (let row = 1; row <= build.construction()!.designs.length && lockedRow === 0; ++row) {
            await clickRow(row);
            if (saysWhy()) lockedRow = row;
        }
        kind = k;
        break;
    }
    await close();
}
expect(kind !== undefined && lockedRow > 0, "a kind of ride with a locked design, its window saying why");
const design = build.construction()!.shown;
log("locked", build.construction()!.locked, "of", build.construction()!.designs.length, "first", design);
await screenshot("locked_design");
await ui.click("design_browser/content/build");
await frames(10);
expect(ui.find("design_browser") !== undefined, "a locked design not built");
await close();

expect(debug.use("unlock_designs") === 1, "the designs unlocked");
expect(debug.use("unlock_designs") === 0, "nothing more to unlock");
expect(await openWindow(kind!), "the window again");
expect(build.construction()!.locked === 0, "none listed locked");
await clickRow(lockedRow);
expect(build.construction()!.shown === design && !saysWhy(), "the same design, with nothing in its way");
await ui.click("design_browser/content/build");
await until(() => ui.find("design_browser") === undefined, 120, "the design built");
quit(0);
