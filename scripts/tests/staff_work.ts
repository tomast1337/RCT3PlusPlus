// SPDX-License-Identifier: GPL-3.0-or-later
//
// Staff at work. A handyman, a mechanic and a guard hired from the staff window go about the park: the
// handymen sweep litter with a broom in hand and water dry flower beds from a can, the mechanic works at a ride
// with their tools, and the guard stops now and then on their rounds to keep watch. Screenshots: each of them at
// it, close up. Watering is shown in a second park, one with flower beds.
//
//   ./nob script scripts/tests/staff_work.ts -- --no-audio

export {};

/** The path of the widget named `name` somewhere under `path`. */

/** Hires each of `hires` from the staff window and lets them go on a path in sight, the camera over the park's
 *  middle path. */
async function hire(hires: readonly (readonly [string, string])[]): Promise<void> {
    const paths = map.paths().filter((p) => !p.queue && !p.raised);
    const spot = paths[Math.floor(paths.length / 2)];
    camera.look({ col: spot.col + 0.5, row: spot.row + 0.5 }, { col: spot.col + 6, row: spot.row + 6, up: 20 });
    await frames(5);
    await ui.click(`tools/Management/${must(ui.children("tools/Management"), "tools/Management shown")[0]}`);
    await until(() => ui.find("tools/Management/Man_Staff") !== undefined, 120, "the management menu");
    await ui.click("tools/Management/Man_Staff");
    await until(
        () => under("Staff_Title", "Staff_HireMaintenanceIcon") !== undefined,
        120,
        "the staff window's hire buttons",
    );
    for (const [button, role] of hires) {
        const count = staff().length;
        await ui.click(must(under("Staff_Title", button), `${button} under Staff_Title`));
        await until(() => staff().length === count + 1, 60, `a ${role} hired`);
        await until(() => staff()[count].state === "held", 30, `the ${role} in hand`);
        const free = paths.find((p) => {
            const on = map.screen(p.col + 0.5, p.row + 0.5);
            return (
                on !== undefined &&
                (on.x < 0.25 || on.x > 0.75) &&
                on.x > 0.05 &&
                on.x < 0.95 &&
                on.y > 0.1 &&
                on.y < 0.85
            );
        });
        expect(free !== undefined, "a path in sight beside the window");
        const at = must(map.screen(free!.col + 0.5, free!.row + 0.5), "the tile in sight");
        pointer.move(at.x, at.y);
        await frames(5);
        await pointer.click(at.x, at.y);
        await until(() => staff()[count].state !== "held", 60, `the ${role} let go`);
    }
    await ui.click(under("Staff_Title", "close") ?? under("Staff_Title", "Close") ?? "Staff_Title");
    await frames(5);
}

await park.open("Campaigns/Base/ChequeredFlag.dat");
await park.ready();
await frames(30);
await park.advanceUntil(() => park.guests() >= 40, 1200, "guests in the park");
await hire([
    ["Staff_HireMaintenanceIcon", "handyman"],
    ["Staff_HireMechanicIcon", "mechanic"],
    ["Staff_HireSecurityIcon", "guard"],
]);

/** Waits for any worker of `role` (the one hired among them) to be at one of `jobs`, then shows them close up. */
async function shoot(role: string, jobs: string[], seconds: number, name: string): Promise<void> {
    // Near the ground (`up` is their height in the world): a ride raised on its station may hide them under its deck.
    const working = () => staff().find((p) => p.role === role && jobs.includes(p.job ?? "none") && (p.up ?? 0) < 3);
    await park.advanceUntil(() => working() !== undefined, seconds, `a ${role} at ${jobs.join(" or ")}`);
    const p = working()!;
    camera.look({ col: p.col!, row: p.row!, up: 1 }, { col: p.col! + 1, row: p.row! + 1, up: 4 });
    // The people's meshes are built beside the frame, a few frames behind the camera.
    await frames(20);
    log(role, JSON.stringify(p));
    await screenshot(name);
}

// Litter strewn where each handyman walks: the park is kept too clean to wait for the guests to drop some.
const handymen = () => staff().filter((p) => p.role === "handyman" && p.state === "walking" && p.col !== undefined);
await park.advanceUntil(() => handymen().length > 0, 120, "a handyman walking");
for (const h of handymen())
    expect(park.dropLitter(Math.floor(h.col!), Math.floor(h.row!), 10), "litter dropped on the path");
await shoot("handyman", ["sweep"], 600, "staff_work_sweep");
await shoot("guard", ["watch"], 600, "staff_work_guard");
await shoot("mechanic", ["fix", "inspect"], 3600, "staff_work_mechanic");

// A park with flower beds, left dry for its handyman to water.
// Its own handyman is given no tasks, so one is hired.
await park.open("Campaigns/Base/VanillaHills.dat");
await park.ready();
await frames(30);
await hire([["Staff_HireMaintenanceIcon", "handyman"]]);
expect(park.dryFlowerBeds() > 0, "flower beds in the park");
await shoot("handyman", ["water"], 900, "staff_work_water");
