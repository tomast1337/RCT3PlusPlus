// SPDX-License-Identifier: GPL-3.0-or-later
//
// The finances window's tabs: a park run on for more months than a park file keeps shows each of them on the
// ledger tab, which writes out as a spreadsheet, the attractions' takings on theirs, and the original layout's
// two tabs when the option asks for it.
//
//   ./nob script scripts/tests/finances.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

/** The path of the widget named `name` somewhere under `path`. */
function under(path: string, name: string): string | undefined {
    for (const child of ui.children(path) ?? []) {
        const at = `${path}/${child}`;
        if (child === name)
            return at;
        const found = under(at, name);
        if (found !== undefined)
            return found;
    }
    return undefined;
}

const kWindow = "FinancialManagement_Title";

async function open_finances(): Promise<void> {
    if (ui.find("tools/Management/Man_Finances") === undefined)
        await ui.click(`tools/Management/${ui.children("tools/Management")![0]}`);
    await until(() => ui.find("tools/Management/Man_Finances") !== undefined, 120, "the management menu");
    await ui.click("tools/Management/Man_Finances");
    await until(() => ui.find(kWindow) !== undefined, 120, "the finances window");
}

async function tab(icon: string): Promise<void> {
    await until(() => under(kWindow, icon) !== undefined, 120, `the ${icon} tab`);
    await ui.click(under(kWindow, icon)!);
    await frames(10);
}

async function close(): Promise<void> {
    const button = under(kWindow, "close");
    if (button !== undefined)
        await ui.click(button);
    await until(() => ui.find(kWindow) === undefined, 120, "the finances window to close");
}

await park.open("Campaigns/Base/Gunslinger.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined, 1200, "the park");

// Seven months on, a week's wages booked in each.
const kMonth = 360;
for (let month = 0; month < 7; ++month) {
    park.setDate((Math.floor(park.date() / kMonth) + 1) * kMonth + 1);
    await park.advance(100);
}
const months = park.history().ledger.length;
log("ledger months", months);
expect(months > 5, "more ledger months than a park file's five");

await open_finances();
await tab("Man_Finances");
expect(under(kWindow, "Rct3pp_Finance_Line") !== undefined, "the ledger's line picker");
await screenshot("ledger");
// Written out, the player is told where.
await ui.click(under(kWindow, "Rct3pp_Finance_Export")!);
await frames(30);
await screenshot("exported");
// The graphs over the last month, zoomed out a few notches with the wheel, and read out under the pointer.
await tab("CashGraphTabIcon");
await ui.click(under(kWindow, "range0")!);
await frames(5);
const graph = ui.find(under(kWindow, "graph")!)!;
pointer.move(graph.right - (graph.right - graph.left) / 3, (graph.top + graph.bottom) / 2);
await frames(3);
for (let notch = 0; notch < 3; ++notch) {
    pointer.wheel(-1);
    await frames(2);
}
await frames(90);
expect((ui.find(under(kWindow, "graph")!)!.tooltip ?? "").includes(":"), "a readout under the pointer");
log("readout", ui.find(under(kWindow, "graph")!)!.tooltip);
await screenshot("graphs_zoomed");
await ui.click(under(kWindow, "range2")!);
await frames(5);
await screenshot("graphs_all");
await tab("AttractionListDialogIconAllAttractions");
await screenshot("attractions");
await close();

// The original layout: the summary and the graphs only.
options.set("original_finances", 1);
await open_finances();
expect(under(kWindow, "CashGraphTabIcon") !== undefined && under(kWindow, "Man_Finances") === undefined, "the original's two tabs");
await screenshot("original_layout");
options.set("original_finances", 0);
quit(0);
