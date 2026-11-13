// SPDX-License-Identifier: GPL-3.0-or-later
//
// Hiring staff from the staff window. Each hire button makes a worker of its job and hands them to the pointer,
// as the pick-up button does; a click on a path puts them down there to walk on, a right click lets them go
// back to where they were made. The list shows them, and an unfolded row's sack button lets its worker go.
// Screenshots: the window with its hire buttons, and the list after hiring.
//
//   ./nob script scripts/tests/staff_hire.ts -- --no-audio

export {};

/** The path of the widget named `name` somewhere under `path`. */

await park.open("Campaigns/Base/ChequeredFlag.dat");
await park.ready();
await frames(30);
const paths = map.paths().filter((p) => !p.queue && !p.raised);
expect(paths.length > 0, "paths in the park");
const spot = paths[Math.floor(paths.length / 2)];
camera.look({ col: spot.col + 0.5, row: spot.row + 0.5 }, { col: spot.col + 6, row: spot.row + 6, up: 20 });
await frames(5);

await ui.click(`tools/Management/${must(ui.children("tools/Management"), "tools/Management shown")[0]}`);
await until(() => ui.find("tools/Management/Man_Staff") !== undefined, 120, "the management menu");
await ui.click("tools/Management/Man_Staff");
await until(() => under("Staff_Title", "Staff_HireMaintenanceIcon") !== undefined, 120, "the staff window's hire buttons");
await frames(5);
await screenshot("staff_menu");

const hires: [string, "handyman" | "mechanic" | "guard" | "entertainer"][] = [
    ["Staff_HireMaintenanceIcon", "handyman"],
    ["Staff_HireMechanicIcon", "mechanic"],
    ["Staff_HireSecurityIcon", "guard"],
    ["Staff_HireEntertainerIcon", "entertainer"],
];
const before = staff().length;
for (const [button, role] of hires) {
    const count = staff().length;
    await ui.click(must(under("Staff_Title", button), `${button} under Staff_Title`));
    await until(() => staff().length === count + 1, 60, `a ${role} hired`);
    const hired = staff()[count];
    expect(hired.role === role, `a ${role}, not ${hired.role}`);
    await until(() => staff()[count].state === "held", 30, `the ${role} in hand`);
    // The entertainer goes back where they were made; the others onto a path in sight beside the window.
    const free = paths.find((p) => {
        const on = map.screen(p.col + 0.5, p.row + 0.5);
        return on !== undefined && (on.x < 0.25 || on.x > 0.75) && on.x > 0.05 && on.x < 0.95 && on.y > 0.1 && on.y < 0.85;
    });
    expect(free !== undefined, "a path in sight beside the window");
    const at = must(map.screen(free!.col + 0.5, free!.row + 0.5), "the tile in sight");
    pointer.move(at.x, at.y);
    await frames(5);
    await pointer.click(at.x, at.y, role === "entertainer" ? "right" : undefined);
    await until(() => staff()[count].state !== "held", 60, `the ${role} let go`);
    // Put back, they walk across the ground to where they were made, which may be far off.
    if (role !== "entertainer")
        await until(() => staff()[count].state === "walking", 600, `the ${role} walking`);
}
expect(staff().length === before + hires.length, "every hire kept");
await frames(60);
await screenshot("staff_list");

// Wages come out of the park's cash as the hours go by.
const cash = park.cash();
await park.advance(20);
log("cash", cash, "->", park.cash());

// The first row unfolded, then its worker sacked from the discipline button.
const list = `${must(under("Staff_Title", "table"), "table under Staff_Title")}/view/content`;
const row = `${list}/${must(ui.children(list), `${list} shown`)[0]}`;
await ui.click(`${row}/Expand`);
await until(() => ui.find(`${row}/Discipline`) !== undefined, 30, "the row unfolded");
await ui.click(`${row}/Discipline`);
await until(() => ui.find(`${row}/Sack`) !== undefined, 30, "the sack button");
const count = staff().length;
await ui.click(`${row}/Sack`);
await until(() => staff().length === count - 1, 60, "the worker sacked");
await frames(60);
await screenshot("staff_sacked");
