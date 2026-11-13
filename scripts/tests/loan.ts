// SPDX-License-Identifier: GPL-3.0-or-later
//
// The finances window's loan buttons: repaying a step takes it from the cash, borrowing one adds it back, the
// cash and the loan moving together.
//
//   ./nob script scripts/tests/loan.ts -- --no-audio

export {};

/** The path of the widget named `name` somewhere under `path`. */

const kStep = 100000; // 1,000.00

await park.open("Campaigns/Base/Gunslinger.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined, 1200, "the park");
expect(park.cash() !== undefined, "a park with money");
park.setCash(10000000);
await ui.click(`tools/Management/${must(ui.children("tools/Management"), "tools/Management shown")[0]}`);
await until(() => ui.find("tools/Management/Man_Finances") !== undefined, 120, "the management menu");
await ui.click("tools/Management/Man_Finances");
await until(() => under("FinancialManagement_Title", "Rct3pp_Borrow") !== undefined, 120, "the borrow button");
await screenshot("loan_buttons");

const loan = park.loan();
const cash = park.cash()!;
const repaid = Math.min(kStep, loan);
await ui.click(
    must(under("FinancialManagement_Title", "Rct3pp_Repay"), "Rct3pp_Repay under FinancialManagement_Title"),
);
await frames(5);
log("repay: loan", loan, "->", park.loan(), "cash", cash, "->", park.cash());
expect(park.loan() === loan - repaid && park.cash() === cash - repaid, `the loan and cash down by ${repaid}`);

await ui.click(
    must(under("FinancialManagement_Title", "Rct3pp_Borrow"), "Rct3pp_Borrow under FinancialManagement_Title"),
);
await frames(5);
log("borrow: loan ->", park.loan(), "cash ->", park.cash());
const borrowed = park.loan() - (loan - repaid);
expect(borrowed > 0 && park.cash() === cash - repaid + borrowed, "the loan and cash up together");
expect(repaid < kStep || borrowed === kStep, `a whole step of ${kStep} borrowed`);
quit(0);
