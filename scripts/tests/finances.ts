// SPDX-License-Identifier: GPL-3.0-or-later
//
// The finances window's tabs: a park run on for more months than a park file keeps shows each of them on the
// ledger tab, which writes out as a spreadsheet; the graphs zoom and read out; the loan's bar borrows; the
// attractions' takings show on their tab; and the original layout has its two tabs when the option asks for it.
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

// The bar's money with an arrow the way last month went, and its profit under the pointer.
const money = ui.find("bar/money")!;
log("bar money", money.text, money.tooltip);
expect(/^[\u2191\u2193]/.test(money.text ?? "") && (money.tooltip ?? "") !== "", "the money's trend");
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
// The loan's bar dragged to its end borrows up to the most, and back repays.
await tab("Attraction_Finances");
const loan_before = park.loan();
const bar = ui.find(under(under(kWindow, "controls")!, "FinancialSummary_Loan")!)!;
// Past its end, a little: the pointer goes by shares of the screen.
await pointer.drag([bar.x, bar.y], [bar.right + 0.02, bar.y], 10);
await frames(70);
log("loan", loan_before, "to", park.loan());
expect(park.loan() > loan_before, "the bar to borrow");
await screenshot("loan");
// Back to nought repays what the cash in hand covers.
const borrowed = park.loan();
await pointer.drag([bar.right - 0.005, bar.y], [bar.left - 0.02, bar.y], 10);
await frames(70);
log("repaid to", park.loan(), "cash", park.cash());
expect(park.loan() < borrowed, "the bar to repay");
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
