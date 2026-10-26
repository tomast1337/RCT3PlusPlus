// SPDX-License-Identifier: GPL-3.0-or-later
//
// The camera's control modes (normal, isometric, free look, advanced), in the game's Vanilla Hills: each mode is picked in turn, its buttons and wheel
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
async function drag(buttons: ("left" | "right" | "middle")[], by: [number, number], count = 10, from: [number, number] = [0.5, 0.5]): Promise<void> {
    pointer.move(from[0], from[1]);
    await frames(2);
    for (const b of buttons)
        b === "left" ? pointer.press() : pointer.button(b, true);
    await frames(1);
    for (let i = 1; i <= count; ++i) {
        pointer.move(from[0] + (by[0] * i) / count, from[1] + (by[1] * i) / count);
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
    // Riders swing over the ride, and a click on one rightly opens their window instead, so a few tries.
    for (let i = 0; i < 5 && rides.shown()?.ride !== ride; ++i) {
        rides.show(other);
        await frames(1 + i * 7);
        await pointer.click(at!.x, at!.y);
        await frames(10);
    }
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

// Free look: the right button looks round from where the eye is, the middle one flies it, the wheel does nothing.
options.set("camera_mode", 1);
await frames(2);
const dist = (a: number[], b: number[]) => Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
before = camera.view();
await drag(["right"], [0.1, 0.05]);
after = camera.view();
expect(!near(after.yaw, before.yaw) && dist(after.eye, before.eye) < 0.1, "free look: the right button to look round from the eye");
before = after;
await drag(["middle"], [0.1, -0.2]);
after = camera.view();
expect(near(after.yaw, before.yaw) && dist(after.eye, before.eye) > 1, "free look: the middle button to fly the eye");
before = after;
pointer.move(0.5, 0.5);
pointer.wheel(2);
await frames(30);
expect(dist(camera.view().eye, before.eye) < 0.1, "free look: the wheel to do nothing");
await clickRide("freelook");

// Advanced: the buttons as the camera controls option shares them out, turning round what the drag started on.
options.set("camera_mode", 0);
options.set("camera_controls", 1);
await frames(2);
before = camera.view();
await drag(["right"], [0.2, 0]);
after = camera.view();
expect(near(after.yaw, before.yaw) && dist(after.target, before.target) > 0.5, "advanced 1: the right button to slide");
before = after;
await drag(["right", "left"], [0.1, 0.05]);
after = camera.view();
expect(!near(after.yaw, before.yaw) && !near(after.pitch, before.pitch), "advanced 1: right and left to turn and tilt");
before = after;
pointer.move(0.5, 0.5);
pointer.wheel(2);
await frames(30);
expect(camera.view().distance < before.distance, "advanced 1: the wheel to zoom");

options.set("camera_controls", 0);
await frames(2);
before = camera.view();
await drag(["right"], [0.1, 0.1]);
after = camera.view();
expect(!near(after.yaw, before.yaw) && !near(after.distance, before.distance) && near(after.pitch, before.pitch), "advanced 0: the right button to turn and zoom");
before = after;
pointer.move(0.5, 0.5);
pointer.wheel(2);
await frames(10);
after = camera.view();
expect(!near(after.pitch, before.pitch) && near(after.distance, before.distance, 1), "advanced 0: the wheel to tilt"); // The last zoom may still be rolling in
await drag(["right", "left"], [0.2, 0]);
expect(dist(camera.view().target, after.target) > 0.5, "advanced 0: right and left to slide");

options.set("camera_controls", 2);
await frames(2);
before = camera.view();
await drag(["right"], [0.1, 0.05]);
after = camera.view();
expect(!near(after.yaw, before.yaw) && !near(after.pitch, before.pitch), "advanced 2: the right button to turn and tilt");

// The point a drag starts on keeps its place on the view while the view turns round it.
options.set("camera_ignores_scenery", 1);
camera.look({ col: site.col + 1.5, row: site.row + 1.5 });
await frames(5);
const pivotTile: [number, number] = [site.col + 4, site.row + 1];
const start = map.screen(...pivotTile)!;
log("pivot", JSON.stringify(start));
await drag(["middle"], [0.08, 0.03], 10, [start.x, start.y]);
const end = map.screen(...pivotTile)!;
log("pivot after", JSON.stringify(end));
expect(near(end.x, start.x, 0.02) && near(end.y, start.y, 0.02), "advanced: the drag to turn round the point it started on");
options.set("camera_ignores_scenery", 0);
await clickRide("advanced");

options.set("camera_mode", 3);
options.set("camera_controls", 1);
quit(0);
