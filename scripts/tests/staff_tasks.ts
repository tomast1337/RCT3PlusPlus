// SPDX-License-Identifier: GPL-3.0-or-later
//
// A worker's Tasks tab holds only their tasks: a line each with its switch near the page's end, lit while the
// task is on. Their patrol area is painted from the Details tab: its patrol button stays down while painting,
// with the button clearing the area beside it and a row of painting tools under the buttons.
// Screenshots: a handyman's tasks, a mechanic's tasks, and a handyman's details while painting.
//
//   ./nob script scripts/tests/staff_tasks.ts -- --no-audio

export {};

/** The path of the widget named `name` somewhere under `path`. */

await park.open("Campaigns/Base/ChequeredFlag.dat");
await park.ready();
await frames(30);
const paths = map.paths().filter((p) => !p.queue && !p.raised);
const spot = paths[Math.floor(paths.length / 2)];
camera.look({ col: spot.col + 0.5, row: spot.row + 0.5 }, { col: spot.col + 6, row: spot.row + 6, up: 20 });
await frames(5);

// A handyman and a mechanic hired from the staff window and set down on a path.
await ui.click(`tools/Management/${must(ui.children("tools/Management"), "tools/Management shown")[0]}`);
await until(() => ui.find("tools/Management/Man_Staff") !== undefined, 120, "the management menu");
await ui.click("tools/Management/Man_Staff");
await until(
    () => under("Staff_Title", "Staff_HireMaintenanceIcon") !== undefined,
    120,
    "the staff window's hire buttons",
);
for (const button of ["Staff_HireMaintenanceIcon", "Staff_HireMechanicIcon"]) {
    const count = staff().length;
    await ui.click(must(under("Staff_Title", button), `${button} under Staff_Title`));
    await until(() => staff().length === count + 1, 60, `${button} hired`);
    await until(() => staff()[count].state === "held", 30, "the worker in hand");
    const free = paths.find((p) => {
        const on = map.screen(p.col + 0.5, p.row + 0.5);
        return (
            on !== undefined && (on.x < 0.25 || on.x > 0.75) && on.x > 0.05 && on.x < 0.95 && on.y > 0.1 && on.y < 0.85
        );
    });
    expect(free !== undefined, "a path in sight beside the window");
    const at = must(map.screen(free!.col + 0.5, free!.row + 0.5), "the tile in sight");
    pointer.move(at.x, at.y);
    await frames(5);
    await pointer.click(at.x, at.y);
    await until(() => staff()[count].state !== "held", 60, "the worker let go");
}
await ui.click(under("Staff_Title", "close") ?? under("Staff_Title", "Close") ?? "Staff_Title");
await frames(5);

for (const role of ["handyman", "mechanic"]) {
    const who = people.list().findIndex((p) => p.role === role);
    expect(who >= 0, `a ${role} in the park`);
    people.show(who);
    await until(() => ui.find("info") !== undefined && info_tab("tools") !== undefined, 120, `the ${role}'s window`);
    await ui.click(`info/${info_tab("tools")}`);
    await frames(10);
    expect(under("info", "Rct3pp_PatrolBrush") === undefined, "no patrol tools on the tasks page");
    await screenshot(`${role}_tasks`);
}

const sweeper = people.list().findIndex((p) => p.role === "handyman");
people.show(sweeper);
await frames(5);
await ui.click(`info/${info_tab("infoicon")}`);
await frames(5);
const patrol = under("info", "Staff_PatrolAreaLarge");
expect(patrol !== undefined, "the patrol button on the details page");
await ui.click(patrol!);
await until(() => under("info", "Rct3pp_PatrolBrush") !== undefined, 60, "the painting tools");
expect(under("info", "PathEditor_Freeform") !== undefined, "the clear button beside the patrol button");
await frames(5);
await screenshot("handyman_painting_patrol");
await ui.click(must(under("info", "Staff_PatrolAreaLarge"), "Staff_PatrolAreaLarge under info"));
await until(() => under("info", "Rct3pp_PatrolBrush") === undefined, 60, "the painting tools gone once done");
