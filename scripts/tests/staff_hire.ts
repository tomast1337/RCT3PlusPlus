// SPDX-License-Identifier: GPL-3.0-or-later
//
// Hiring staff from the staff window. Each hire button makes a worker of its job and hands them to the pointer,
// as the pick-up button does; a click on a path puts them down there to walk on, a right click lets them go
// back to where they were made. The list shows them with their wage, and the sack button lets the selected one go.
// Screenshots: the window with its hire buttons, and the list after hiring.
//
//   ./nob script scripts/tests/staff_hire.ts -- --no-audio

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

await park.open("Campaigns/Base/ChequeredFlag.dat");
await park.ready();
await frames(30);
const paths = map.paths().filter((p) => !p.queue && !p.raised);
expect(paths.length > 0, "paths in the park");
const spot = paths[Math.floor(paths.length / 2)];
camera.look({ col: spot.col + 0.5, row: spot.row + 0.5 }, { col: spot.col + 6, row: spot.row + 6, up: 20 });
await frames(5);

await ui.click(`tools/Management/${ui.children("tools/Management")![0]}`);
await until(() => ui.find("tools/Management/Man_Staff") !== undefined, 120, "the management menu");
await ui.click("tools/Management/Man_Staff");
await until(() => under("Staff_Title", "Staff_HireMaintenanceIcon") !== undefined, 120, "the staff window's hire buttons");
await frames(5);
await screenshot("staff_menu");

const staff = () => people.list().filter((p) => p.staff);
const hires: [string, "handyman" | "mechanic" | "guard" | "other"][] = [
    ["Staff_HireMaintenanceIcon", "handyman"],
    ["Staff_HireMechanicIcon", "mechanic"],
    ["Staff_HireSecurityIcon", "guard"],
    ["Staff_HireEntertainerIcon", "other"],
];
const before = staff().length;
for (const [button, role] of hires) {
    const count = staff().length;
    await ui.click(under("Staff_Title", button)!);
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
    const at = map.screen(free!.col + 0.5, free!.row + 0.5)!;
    pointer.move(at.x, at.y);
    await frames(5);
    await pointer.click(at.x, at.y, role === "other" ? "right" : undefined);
    await until(() => staff()[count].state !== "held", 60, `the ${role} let go`);
    // Put back, they walk across the ground to where they were made, which may be far off.
    if (role !== "other")
        await until(() => staff()[count].state === "walking", 600, `the ${role} walking`);
}
expect(staff().length === before + hires.length, "every hire kept");
await frames(60);
await screenshot("staff_list");

// Wages come out of the park's cash as the hours go by.
const cash = park.cash();
await park.advance(20);
log("cash", cash, "->", park.cash());

// The first row selected, then sacked.
const table = ui.find(under("Staff_Title", "table")!)!;
await pointer.click(table.left + (table.right - table.left) * 0.3, table.top + (table.bottom - table.top) * 0.09);
await frames(3);
const count = staff().length;
await ui.click(under("Staff_Title", "Staff_SackIconLarge")!);
await until(() => staff().length === count - 1, 60, "the selected worker sacked");
await frames(60);
await screenshot("staff_sacked");
