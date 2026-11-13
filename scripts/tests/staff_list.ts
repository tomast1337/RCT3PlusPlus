// SPDX-License-Identifier: GPL-3.0-or-later
//
// The staff window's list: one row per worker of the jobs shown, sorted by the drop-down's choice. A row's box
// unfolds it into the worker's tasks, laziness, discipline, training, wage and start; the window's plus unfolds
// every row. Each job's switch along the bottom hides or shows its workers, and asking for a keeper with no
// animal house says one is needed.
// Screenshots: the list folded, one row unfolded, every row unfolded, the list sorted by name with the
// handymen hidden, and the enclosure message.
//
//   ./nob script scripts/tests/staff_list.ts -- --no-audio

export {};

/** The path of the widget named `name` somewhere under `path`. */

await park.open("Campaigns/Base/ChequeredFlag.dat");
await park.ready();
await frames(30);

await ui.click(`tools/Management/${must(ui.children("tools/Management"), "tools/Management shown")[0]}`);
await until(() => ui.find("tools/Management/Man_Staff") !== undefined, 120, "the management menu");
await ui.click("tools/Management/Man_Staff");
await until(() => under("Staff_Title", "Staff_HireMaintenanceIcon") !== undefined, 120, "the staff window");

// One of each job the window hires, let go where they were made.
for (const button of [
    "Staff_HireMaintenanceIcon",
    "Staff_HireMechanicIcon",
    "Staff_HireSecurityIcon",
    "Staff_HireEntertainerIcon",
]) {
    const count = staff().length;
    await ui.click(must(under("Staff_Title", button), `${button} under Staff_Title`));
    await until(() => staff().length === count + 1 && staff()[count].state === "held", 60, "a worker in hand");
    pointer.move(0.1, 0.5);
    await frames(3);
    await pointer.click(0.1, 0.5, "right");
    await until(() => staff()[count].state !== "held", 60, "the worker let go");
}
const list = `${must(under("Staff_Title", "table"), "table under Staff_Title")}/view/content`;
const rows = () => ui.children(list) ?? [];
// Vendors, at their stalls, stay out of the list until their switch is on.
await until(() => rows().length >= 4, 120, "a row for each worker hired");
log("rows", rows().length, "staff", staff().length);
await frames(10);
await screenshot("staff_list_folded");

const first = `${list}/${rows()[0]}`;
await ui.click(`${first}/Expand`);
await until(() => ui.find(`${first}/Wage`) !== undefined, 30, "the first row unfolded");
await frames(5);
await screenshot("staff_list_row_unfolded");

await ui.click(must(under("Staff_Title", "ExpandAll"), "ExpandAll under Staff_Title"));
await until(
    () =>
        rows().every(
            (r) => ui.find(`${list}/${r}/Hired`) !== undefined || ui.find(`${list}/${r}/Patrol`) === undefined,
        ),
    30,
    "every row unfolded",
);
await frames(5);
await screenshot("staff_list_all_unfolded");
await ui.click(must(under("Staff_Title", "ContractAll"), "ContractAll under Staff_Title"));

// Sorted by name, the second choice, and the handymen hidden.
await ui.click(must(under("Staff_Title", "SortBy"), "SortBy under Staff_Title"));
await until(() => ui.find("SortBy.menu") !== undefined, 30, "the sort choices");
const menu = must(ui.find("SortBy.menu"), "SortBy.menu shown");
await pointer.click((menu.left + menu.right) / 2, menu.top + ((menu.bottom - menu.top) * 1.5) / 12);
const shown = rows().length;
await ui.toggle(must(under("Staff_Title", "Staff_ViewMaintenance"), "Staff_ViewMaintenance under Staff_Title"), false);
await until(() => rows().length < shown, 60, "the handymen hidden");
await frames(10);
await screenshot("staff_list_sorted_filtered");

// A keeper needs an animal house to look after.
await ui.click(
    must(under("Staff_Title", "Staff_HireAnimalKeeperIcon"), "Staff_HireAnimalKeeperIcon under Staff_Title"),
);
await until(() => ui.find("StaffQuestion") !== undefined, 30, "the enclosure message");
await frames(5);
await screenshot("staff_list_enclosure_needed");
