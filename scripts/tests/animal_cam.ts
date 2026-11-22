// SPDX-License-Identifier: GPL-3.0-or-later
//
// AnimalCam on a zoo animal of the game's Scrub Gardens: entered from the animal's window, it follows the animal
// from behind, then the type button turns it to a static camera and to the animal's head, and leaving brings the
// overhead view and the windows back. The next-train buttons go to the park's other animals. Reflect Animals
// shows an animal in a lake's reflection when on and not when off, and the animal detail and draw distance
// options draw animals farther or not at all.
// Screenshots: each view; an animal by water with its reflection on and off; animals far off at the least and
// most draw distance.
//
//   ./nob script scripts/tests/animal_cam.ts -- --no-intro --no-audio --seed 1

export {};

await park.open("Campaigns/Base/Wild/ScrubGardens.dat");
await park.ready();
await frames(30);

const out = animals.list().filter((a) => !a.crated && !a.escaped);
const first = must(out[0], "an animal out of its crate");
animals.open(first.id);
await until(() => animals.shown()?.id === first.id, 60, "the animal's window");
await ui.click(`info/${must(info_tab("guest_options"), "the details tab")}`);
await frames(5);

// Entered from its window, AnimalCam follows it, with everything else put away.
expect(camera.riding() === undefined, "the overhead view first");
await ui.click(must(under("info", "AnimalCam"), "the AnimalCam button"));
await until(() => camera.riding()?.eye !== undefined, 60, "the view with the animal");
const riding = camera.riding()!;
log("riding", JSON.stringify(riding));
expect(
    riding.kind === "follow" && "animal" in riding && riding.animal === first.id,
    `following the animal, not ${JSON.stringify(riding)}`,
);
expect(ui.find("info") === undefined && ui.find("bar") === undefined, "the rest put away");
expect(ui.find("ride_camera/RidecamGUI_Exit") !== undefined, "the camera panel");
pointer.move(0.4, 0.4);
await frames(20);
await screenshot("follow");

// The type button goes Follow, Static, Head, and round to Follow.
for (const kind of ["static", "head", "follow"]) {
    pointer.move(0.6, 0.6);
    await ui.click("ride_camera/RidecamGUI_Type");
    await until(() => camera.riding()?.kind === kind && camera.riding()?.eye !== undefined, 30, `the ${kind} view`);
    const eye = camera.riding()!.eye!;
    log(kind, JSON.stringify(camera.riding()));
    if (kind !== "follow") {
        pointer.move(0.4, 0.4);
        await frames(20);
        await screenshot(kind);
    }
    expect(
        eye.every((v) => Number.isFinite(v)),
        `the ${kind} view somewhere`,
    );
}

// The train buttons go to another animal and back.
if (out.length > 1) {
    await ui.click("ride_camera/RidecamGUI_NextTrain");
    await frames(2);
    const next = camera.riding()!;
    expect("animal" in next && next.animal !== first.id, "another animal");
    await ui.click("ride_camera/RidecamGUI_PrevTrain");
    await frames(2);
    const back = camera.riding()!;
    expect("animal" in back && back.animal === first.id, "back to the first animal");
}

// Leaving brings the overhead view and the windows back.
await ui.click("ride_camera/RidecamGUI_Exit");
await until(() => camera.riding() === undefined, 30, "the overhead view again");
expect(ui.find("bar") !== undefined, "the bar back");

// A pond dug by the first animal, and the animal nearest it seen across the water: in the pond's reflection only with
// Reflect Animals. Paused, so the animals stay where they are. The park has no water near its animals.
await ui.click("bar/Top_Pause");
await frames(2);
const by = must(
    animals.list().find((x) => x.id === first.id),
    "the first animal",
);
// A crater three tiles off, clear of the path it stands by.
const pond = { col: Math.floor(by.at.col) + 3, row: Math.floor(by.at.row) };
/** Picks a tool from the land menu, opening the menu when it is shut. */
async function pick(icon: string): Promise<void> {
    if (!ui.find(`tools/Land/${icon}`)) await ui.click("tools/Land/Land");
    await ui.click(`tools/Land/${icon}`);
}
camera.look({ col: pond.col + 1.5, row: pond.row + 0.5 }, { col: pond.col + 0.5, row: pond.row - 2, up: 60 });
await frames(2);
await pick("Land_Brush_Height");
await until(() => ui.find("land_panel/Land_Brush_Height_Mode") !== undefined, 60, "the land panel");
await ui.click("land_panel/Land_Brush_Height_Mode");
await until(() => ui.find("land_panel/body/Land_Brush_Height_Mode_Drag") !== undefined, 60, "the mode tab");
await ui.click("land_panel/body/Land_Brush_Height_Mode_Drag");
await ui.click("land_panel/Land_Brush_Height_Raise");
await until(() => ui.find("land_panel/body/Land_Brush_Height_Lower_Crater") !== undefined, 60, "the crater brush");
await ui.click("land_panel/body/Land_Brush_Height_Lower_Crater");
const [px, py] = on_view(pond.col, pond.row);
await pointer.drag([px, py], [px, py + 0.08], 30);
await frames(2);
// Clicked on a corner of the pit's side, well down it, the pond fills to there.
let side: { col: number; row: number; up: number } | undefined;
for (let dc = -6; dc <= 6; ++dc)
    for (let dr = -6; dr <= 6; ++dr) {
        const up = map.tile(pond.col + dc, pond.row + dr)?.corners[0];
        if (up !== undefined && up < -1.2 && (side === undefined || Math.abs(up + 2.5) < Math.abs(side.up + 2.5)))
            side = { col: pond.col + dc, row: pond.row + dr, up };
    }
const at = must(side, "a corner of the pit's side");
// From across the pit, so that side faces the view.
camera.look(
    { col: at.col, row: at.row, up: at.up },
    { col: 2 * pond.col - at.col, row: 2 * pond.row - at.row, up: 30 },
);
await frames(2);
await pick("Land_Water");
const click = must(map.screen(at.col, at.row, at.up), "the pit's side in sight");
await pointer.click(click.x, click.y);
await frames(2);
await pick("Land_Water"); // Put away
// The middle of the wet tiles round it.
const wets: { col: number; row: number; level: number }[] = [];
for (let dc = -8; dc <= 8; ++dc)
    for (let dr = -8; dr <= 8; ++dr) {
        const [col, row] = [Math.floor(by.at.col) + dc, Math.floor(by.at.row) + dr];
        const level = map.tile(col, row)?.water;
        if (level !== undefined) wets.push({ col: col + 0.5, row: row + 0.5, level });
    }
const wet =
    wets.length === 0
        ? undefined
        : {
              col: wets.reduce((t, w) => t + w.col, 0) / wets.length,
              row: wets.reduce((t, w) => t + w.row, 0) / wets.length,
              level: wets[0].level,
          };
const water = must(wet, "a pond near the animal");
// Seen from the far side, low: the eye's height over the water and the animal's set how far back the eye goes for the
// animal's mirror image to fall in the pond's middle.
const near = animals
    .list()
    .filter((x) => !x.crated && !x.escaped)
    .reduce((m, x) =>
        Math.hypot(x.at.col - water.col, x.at.row - water.row) < Math.hypot(m.at.col - water.col, m.at.row - water.row)
            ? x
            : m,
    );
log("pond", JSON.stringify(water), "nearest", near.name);
const off = Math.hypot(near.at.col - water.col, near.at.row - water.row);
const [high, body] = [1.3, 2.4]; // the eye just over the ground, and the animal's back, over the water. Chosen by eye
const back = (off * high) / body; // from the water, on the far side: similar triangles about the mirror point
for (const on of [1, 0]) {
    options.set("reflect_animals", on);
    camera.look(
        { col: water.col, row: water.row, up: water.level },
        {
            col: water.col - ((near.at.col - water.col) / off) * back,
            row: water.row - ((near.at.row - water.row) / off) * back,
            up: water.level + high,
        },
    );
    await frames(10);
    await screenshot(on ? "reflect_on" : "reflect_off");
}

// Far off, at the least draw distance no animal is drawn, at the most they are.
camera.look(
    { col: first.at.col, row: first.at.row, up: 0 },
    { col: first.at.col - 30, row: first.at.row - 30, up: 60 },
);
for (const [detail, distance, name] of [
    [3, 3, "most"],
    [0, 0, "least"],
] as const) {
    options.set("animal_detail", detail);
    options.set("animal_distance", distance);
    await frames(10);
    await screenshot(`distance_${name}`);
}
options.set("animal_detail", 1);
options.set("animal_distance", 1);

quit();
