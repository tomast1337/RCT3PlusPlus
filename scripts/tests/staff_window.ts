// SPDX-License-Identifier: GPL-3.0-or-later
//
// A worker's details page as the game lays it out: a short name box, what they are doing under it, then the
// row of round buttons (pick up, thoughts to the messages, highlight, patrol, warn, sack, train). The patrol
// painting tools show only while the area is painted. An entertainer's costume tab shows the costume they wear.
// Screenshots: an entertainer's and a handyman's details.
//
//   ./nob script scripts/tests/staff_window.ts -- --no-audio

export {};

await park.open("Campaigns/Base/Gunslinger.dat");
await park.ready();
await frames(30);
const paths = map.paths().filter((p) => !p.queue && !p.raised);
const spot = paths[Math.floor(paths.length / 2)];
camera.look({ col: spot.col + 0.5, row: spot.row + 0.5 }, { col: spot.col + 6, row: spot.row + 6, up: 20 });
await frames(5);

// An entertainer and a handyman hired from the staff window and set down on a path.
await ui.click(`tools/Management/${must(ui.children("tools/Management"), "tools/Management shown")[0]}`);
await until(() => ui.find("tools/Management/Man_Staff") !== undefined, 120, "the management menu");
await ui.click("tools/Management/Man_Staff");
await until(
    () => under("Staff_Title", "Staff_HireMaintenanceIcon") !== undefined,
    120,
    "the staff window's hire buttons",
);
for (const button of ["Staff_HireEntertainerIcon", "Staff_HireMaintenanceIcon"]) {
    const count = staff().length;
    await ui.click(must(under("Staff_Title", button), `${button} under Staff_Title`));
    await until(() => staff().length === count + 1, 60, `${button} hired`);
    await until(() => staff()[count].state === "held", 30, "the worker in hand");
    const free = paths.find((p) => {
        const on = map.screen(p.col + 0.5, p.row + 0.5);
        return on !== undefined && on.x > 0.05 && on.x < 0.45 && on.y > 0.1 && on.y < 0.6;
    });
    expect(free !== undefined, "a path in sight away from the window");
    const at = must(map.screen(free!.col + 0.5, free!.row + 0.5), "the tile in sight");
    pointer.move(at.x, at.y);
    await frames(5);
    await pointer.click(at.x, at.y);
    await until(() => staff()[count].state !== "held", 60, "the worker let go");
}
await ui.click(under("Staff_Title", "close") ?? under("Staff_Title", "Close") ?? "Staff_Title");
await frames(5);

const row = ["PickUp", "Staff_PatrolAreaLarge", "Staff_WarnIconLarge", "Staff_SackIconLarge", "StaffWindow_TrainHelp"];
for (const role of ["entertainer", "handyman"]) {
    const who = must(
        people.list().find((p) => p.staff && p.role === role),
        `a ${role} in the park`,
    ).index;
    people.show(who);
    await until(() => ui.find("info") !== undefined && info_tab("infoicon") !== undefined, 120, `the ${role}'s window`);
    await ui.click(`info/${info_tab("infoicon")}`);
    await frames(30);
    for (const name of row) expect(under("info", name) !== undefined, `${name} on the ${role}'s details`);
    expect(under("info", "PathEditor_Freeform") === undefined, "no clear button out of patrol painting");
    expect(under("info", "Rct3pp_PatrolBrush") === undefined, "no patrol tools out of patrol painting");
    const name = must(ui.find(must(under("info", "PersonName"), "the name box")), "the name box shown");
    expect(name.right - name.left < 200, "a short name box, not the page's width");
    const doing = must(ui.find(must(under("info", "PersonDoing_label"), "the status line")), "the status line shown");
    expect((doing.text ?? "") !== "", `what the ${role} is doing under the name`);
    const body = must(ui.find(must(under("info", "body"), "the page")), "the page shown");
    log(
        role,
        "doing",
        doing.text,
        "name box",
        name.left,
        name.top,
        name.right,
        name.bottom,
        "page",
        body.left,
        body.top,
        body.right,
        body.bottom,
    );
    if (role === "entertainer")
        expect(info_tab("entertainercostume_") !== undefined, "the costume tab shows the costume worn");
    await screenshot(`${role}_details`);
}
quit();
