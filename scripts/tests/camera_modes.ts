// SPDX-License-Identifier: GPL-3.0-or-later
//
// The camera's control modes, in the game's Vanilla Hills: each mode is picked in turn, its buttons and wheel
// moved over the park, and what they did to the camera checked; a click on a flat ride then opens its window,
// so what is under the pointer still follows the view.
//
//   ./nob script scripts/tests/camera_modes.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

const near = (a: number, b: number, by = 1e-2) => Math.abs(a - b) < by;
/** `a` less `b`, taken round a full turn into -pi..pi. */
const turn = (a: number, b: number) => Math.atan2(Math.sin(a - b), Math.cos(a - b));

await park.open("Campaigns/Base/VanillaHills.dat");
await park.ready();
await frames(60);
const ride = rides.list().findIndex((r) => r.flat && r.site !== null);
expect(ride >= 0, "a flat ride");
const site = rides.list()[ride].site!;
const other = ride === 0 ? 1 : 0; // Shown before each click, so the click must change it
expect(rides.list().length > 1, "two rides");

/** Drags with `buttons` from the middle of the view `by` shares across and down. */
async function drag(buttons: ("left" | "right" | "middle")[], by: [number, number], count = 10): Promise<void> {
    pointer.move(0.5, 0.5);
    await frames(2);
    for (const b of buttons)
        b === "left" ? pointer.press() : pointer.button(b, true);
    await frames(1);
    for (let i = 1; i <= count; ++i) {
        pointer.move(0.5 + (by[0] * i) / count, 0.5 + (by[1] * i) / count);
        await frames(1);
    }
    for (const b of buttons)
        b === "left" ? pointer.release() : pointer.button(b, false);
    await frames(2);
}

/** Looks at the flat ride, clicks it and expects its window. */
async function clickRide(mode: string): Promise<void> {
    rides.show(other);
    await frames(2);
    expect(rides.shown()?.ride === other, "another ride's window first");
    // Its site is a corner; it stands over the tiles past it.
    camera.look({ col: site.col + 1.5, row: site.row + 1.5 });
    await frames(10);
    const at = map.screen(site.col + 1, site.row + 1);
    expect(at !== undefined, `the ride in sight in ${mode}`);
    await pointer.click(at!.x, at!.y);
    await frames(10);
    expect(rides.shown()?.ride === ride, `a click on the ride to open its window in ${mode}`);
    await screenshot(mode);
}

// Normal: the right button slides, the right with the left turns and tilts, the wheel zooms.
options.set("camera_mode", 3);
await frames(2);
let before = camera.view();
await drag(["right"], [0.2, 0]);
let after = camera.view();
expect(near(after.yaw, before.yaw) && !near(after.target[0], before.target[0], 0.5) || !near(after.target[2], before.target[2], 0.5), "normal: the right button to slide the view");
before = after;
await drag(["right", "left"], [0.1, 0.05]);
after = camera.view();
expect(!near(after.yaw, before.yaw) && !near(after.pitch, before.pitch), "normal: right and left to turn and tilt");
await clickRide("normal");

// Isometric: faces a diagonal at a fixed tilt, turns a quarter at a time, and the middle button does nothing.
options.set("camera_mode", 2);
await frames(60);
before = camera.view();
log("isometric", JSON.stringify(before));
expect(near(before.pitch, Math.PI / 4), "isometric: tilted half way down");
expect(near(turn(before.yaw, Math.PI / 4) % (Math.PI / 2), 0) || near(Math.abs(turn(before.yaw, Math.PI / 4) % (Math.PI / 2)), Math.PI / 2), "isometric: facing a diagonal");
await drag(["right", "left"], [0.3, 0.1]);
await frames(60);
after = camera.view();
log("isometric turned", JSON.stringify(after));
expect(near(Math.abs(turn(after.yaw, before.yaw)), Math.PI / 2), "isometric: a quarter turn from right and left");
expect(near(after.pitch, Math.PI / 4), "isometric: the tilt kept");
before = after;
await drag(["middle"], [0.2, 0.2]);
after = camera.view();
expect(near(after.yaw, before.yaw) && near(after.target[0], before.target[0]) && near(after.target[2], before.target[2]), "isometric: the middle button to do nothing");
pointer.move(0.5, 0.5);
pointer.wheel(2);
await frames(30);
expect(camera.view().distance < after.distance, "isometric: the wheel to zoom");
await clickRide("isometric");

options.set("camera_mode", 3);
quit(0);
