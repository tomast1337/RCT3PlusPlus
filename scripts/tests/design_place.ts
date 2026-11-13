// SPDX-License-Identifier: GPL-3.0-or-later
//
// Placing a saved design whole, in a placing mode of its own. Its round panel turns it (its price beside the turn),
// shows its window's page with its name to type, its tune and why it may not stand where it is, paints it, and cuts
// the ground it passes into smoothly or sharply, or with neither lit refuses ground in its way. Its ghost is red
// while refused: off the park's land, lifted too high over the ground, in the way of what stands, into the ground,
// or, for a pool slide, with its pool sides off a pool's edge. Laid, it takes the name typed, its building closes and
// its window opens. Give it a park with a pool and a free edge, such as one with two pools:
//
//   ./nob script scripts/tests/design_place.ts -- --no-intro --no-audio --seed 1 --park PARK

export {};

const kPanel = "design_placing";
const kSteps: [number, number][] = [[0, 1], [1, 0], [0, -1], [-1, 0]]; // By side, as the pools count them
type Spot = { col: number; row: number };

/** Picks the saved design of `kind` whose file names `design` (the first when none), leaving it in hand. */
async function pickDesign(overlay: string, kind: string, design?: string): Promise<number> {
    expect(build.pick(overlay, kind, "designs"), `a ${kind} to build`);
    await until(() => ui.find("design_browser") !== undefined, 600, "the designs window");
    await frames(3);
    const row = design === undefined ? 0 : build.construction()!.designs.findIndex((d) => d.includes(design));
    expect(row >= 0, `the ${design} design among ${build.construction()!.designs.join(", ")}`);
    const list = must(ui.find("design_browser/content/list"), "design_browser/content/list shown");
    const button = must(ui.find("design_browser/content/build"), "design_browser/content/build shown");
    const unit = (button.bottom - button.top) / 22;
    // Under the column heads and the custom track's row, which stays first.
    await pointer.click(list.x, list.top + (18 + 16 * (row + 1) + 8) * unit);
    await frames(5);
    await ui.click("design_browser/content/build");
    await frames(10);
    expect(ui.find("design_browser") === undefined, "the window closed by Build");
    return build.construction()!.ride;
}

let last = { x: 0.5, y: 0.5 }; // Where the pointer was left

/** Looks down on tile `at` from the south and leaves the pointer over it. */
async function over(at: Spot): Promise<void> {
    camera.look({ col: at.col, row: at.row }, { col: at.col, row: at.row + 30, up: 45 });
    await frames(4);
    last = must(map.screen(at.col, at.row), `tile ${at.col}, ${at.row} in sight`);
    pointer.move(last.x, last.y);
    await frames(4);
}

/** A click where the pointer was left: laying the design in hand there, if it may stand. */
async function lay(): Promise<void> {
    await pointer.click(last.x, last.y);
    await frames(30);
}

/** Moves the pointer until the design's first piece stands on `goal`; whether it does. */
async function aim(goal: Spot): Promise<boolean> {
    let at = { ...goal };
    for (let i = 0; i < 4; ++i) {
        await over(at);
        const g = build.construction()!.ghostAt;
        if (g === null)
            return false;
        if (g.col === goal.col && g.row === goal.row)
            return true;
        at = { col: at.col + goal.col - g.col, row: at.row + goal.row - g.row };
    }
    return false;
}

const status = () => ui.find(`${kPanel}/body/status_label`)?.text ?? "";
const refusal = () => build.construction()?.refusal ?? "gone";

// ── 1. A ring slide by a pool: refused off the pool's edge, laid once its pool sides meet the water ──
await until(() => park.loaded(), 1200, "the park");
await until(() => build.flatRides() !== undefined, 1200, "the build items");
await frames(10);
const water = map.pools().squares.filter((s) => s.kind === "water");
const pooled = new Set(map.pools().squares.map((s) => `${s.col},${s.row}`));
const wet = new Set(water.map((s) => `${s.col},${s.row}`));
expect(water.length > 0, "a pool in the park");
const slide = await pickDesign("tracks/TrackedRides/RingSlide/RingSlide", "RingSlide", "Undertow");
expect(ui.find(kPanel) !== undefined && ui.find("placing") === undefined, "the design's own round panel, not the construction panel");
const tabs = ui.children(kPanel) ?? [];
for (const tab of ["IconAttractionEditorRotate", "Guest_Rides", "Attraction_Colour", "TerrainFlattener_SmoothOff", "TerrainFlattener_SharpOff"])
    expect(tabs.includes(tab), `the ${tab} tab, among ${tabs.join(", ")}`);
const price = ui.find(`${kPanel}/view_text`)?.text ?? "";
expect(/\d/.test(price), `the design's price on the disc, not "${price}"`);
expect(ui.find(`${kPanel}/body/TT_AttractionDialogIconLabelButton`)?.text === "Undertow", "the design's name to type over");
log("undertow price", price);

// Away from the pools: refused for its pool sides.
await over({ col: 20, row: 20 });
expect(refusal() === "PlaceObject_MustEndOnPoolEdge", `the slide refused off a pool's edge, not ${refusal()}`);
expect(status() !== "" && status() === must(ui.find(`${kPanel}/body/status_label`), `${kPanel}/body/status_label shown`).text, "why, on its page");
log("off the edge:", status());
await screenshot("design_place_off_pool_edge");
const away = rides.list()[slide].pieces ?? 0;
await lay();
expect((rides.list()[slide].pieces ?? 0) === away && build.construction() !== undefined, "nothing laid off the edge, the design still in hand");

// Each way round, where its pool sides would face water across them from dry ground: tried until one may stand.
let fitted: Spot | undefined;
for (let turn = 0; turn < 4 && fitted === undefined; ++turn) {
    await over({ col: 20, row: 20 });
    const c = build.construction()!;
    const g = c.ghostAt!;
    const rel = c.poolSides.map((s) => ({ col: s.col - g.col, row: s.row - g.row, side: s.side }));
    expect(rel.length > 0, "pool sides on the slide");
    const meets = (at: Spot) => rel.every((r) => {
        const [dc, dr] = kSteps[r.side];
        return wet.has(`${at.col + r.col + dc},${at.row + r.row + dr}`) && !pooled.has(`${at.col + r.col},${at.row + r.row}`);
    });
    for (const w of water) {
        const [dc, dr] = kSteps[rel[0].side];
        const at = { col: w.col - dc - rel[0].col, row: w.row - dr - rel[0].row };
        if (!meets(at) || !(await aim(at)))
            continue;
        log("turn", turn, "tried", JSON.stringify(at), refusal());
        if (refusal() === "") {
            fitted = at;
            break;
        }
    }
    if (fitted === undefined) {
        await ui.click(`${kPanel}/IconAttractionEditorRotate`);
        await frames(3);
        expect(build.construction()!.turn === (turn + 1) % 4, "a quarter turn from the turn tab");
    }
}
expect(fitted !== undefined, "a pool edge the slide fits");
expect(status() === "", `nothing said once it may stand, not "${status()}"`);
await screenshot("design_place_pool_edge");
await lay();
const laid = rides.list()[slide];
log("undertow laid", laid.pieces, "pieces");
expect(laid.pieces === 56, `the Undertow laid whole, not ${laid.pieces} pieces`);
expect(build.construction() === undefined, "its building closed once laid");
expect(ui.find("info/title")?.text === "Undertow", `its window open, not ${ui.find("info/title")?.text}`);
expect(build.placing() === undefined, "no entrance asked for a ride in a pool");
await screenshot("design_place_slide_laid");

// ── 2. A coaster on open ground ──
await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "tools");
const { width, height } = map.size();
const middle = { col: Math.floor(width / 2), row: Math.floor(height / 2) };
const coaster = await pickDesign("tracks/coasters/Corkscrew/Corkscrew", "Corkscrew");
await over(middle);
expect(refusal() === "" && !build.construction()!.clashes, `the design free on open ground, not ${refusal()}`);
expect(build.construction()!.ghostDoors === 2, "the design's entrance and exit in its ghost");
await screenshot("design_place_window");

// Turned a quarter each press of the turn tab, and by its key.
const turn = build.construction()!.turn;
await ui.click(`${kPanel}/IconAttractionEditorRotate`);
await frames(3);
expect(build.construction()!.turn === (turn + 1) % 4, "a quarter turn from the turn tab");
await over(middle);
await screenshot("design_place_turned");
keys.press(".");
await frames(3);
expect(build.construction()!.turn === (turn + 2) % 4, `a quarter turn more from its key, not ${build.construction()!.turn}`);
for (let i = 0; i < 2; ++i) {
    await ui.click(`${kPanel}/IconAttractionEditorRotate`);
    await frames(3);
}
expect(build.construction()!.turn === turn, "turned back round");

// Lifted with Shift high over the ground: too high.
await over(middle);
const ground = build.construction()!.ghostAt!.height;
const p = must(map.screen(middle.col, middle.row), `tile ${middle.col}, ${middle.row} in sight`);
pointer.shift(true);
await frames(3);
for (let i = 1; i <= 60 && refusal() !== "PlaceObject_TooHigh"; ++i) {
    pointer.move(p.x, p.y - 0.01 * i);
    await frames(3);
}
expect(refusal() === "PlaceObject_TooHigh", `the design refused lifted high, not ${refusal()}`);
log("too high at", build.construction()!.ghostAt!.height, "over", ground, ":", status());
await screenshot("design_place_too_high");
await pointer.click(p.x, p.y - 0.01 * 60);
await frames(5);
expect((rides.list()[coaster].pieces ?? 0) === 0, "nothing laid too high");
// Down to the ground again.
for (let i = 0; i <= 80 && build.construction()!.ghostAt!.height > ground; ++i) {
    pointer.move(p.x, p.y - 0.6 + 0.01 * i);
    await frames(3);
}
pointer.shift(false);
await frames(3);
expect(build.construction()!.ghostAt!.height === ground, "the design down on the ground again");

// Named, then laid: its building closes and its window opens with the name typed.
await over(middle);
await ui.click(`${kPanel}/body/TT_AttractionDialogIconLabelButton`);
keys.press("Ctrl+A");
keys.type("Design Corkscrew");
keys.press("Enter");
await frames(3);
await over(middle);
expect(refusal() === "", `the design free again, not ${refusal()}`);
const cash = park.cash();
await lay();
const first = rides.list()[coaster];
log("coaster laid", first.pieces, "pieces for", cash !== undefined ? cash - park.cash()! : "-");
expect((first.pieces ?? 0) > 0 && first.entrance !== null && first.exit !== null, `the design laid with its doors, not ${JSON.stringify(first)}`);
expect(first.name === "Design Corkscrew", `the name typed, not ${first.name}`);
expect(build.construction() === undefined, "its building closed once laid");
expect(ui.find("info/title")?.text === "Design Corkscrew", `its window open, not ${ui.find("info/title")?.text}`);
await screenshot("design_place_coaster_laid");

// Over the first: in its way.
const second = await pickDesign("tracks/coasters/Corkscrew/Corkscrew", "Corkscrew");
await over(middle);
expect(refusal() === "PlaceObject_Intersects", `the second refused over the first, not ${refusal()}`);
log("in the way:", status());
await screenshot("design_place_intersects");
await lay();
expect((rides.list()[second].pieces ?? 0) === 0 && build.construction()?.ride === second, "nothing of it laid, still in hand");
await ui.click(`${kPanel}/close`);
await frames(5);
expect(build.construction() === undefined && ui.find(kPanel) === undefined, "put away by its panel's close button");

// ── 3. Into a hill: refused with neither ground tab lit, laid once the ground is cut ──
async function land(icon: string): Promise<void> {
    if (!ui.find(`tools/Land/${icon}`))
        await ui.click("tools/Land/Land");
    await ui.click(`tools/Land/${icon}`);
}
const hill = { col: middle.col - 20, row: middle.row + 24 };
await over(hill);
await land("Land_Brush_Height");
await until(() => ui.find("land_panel/Land_Brush_Height_Raise") !== undefined, 60, "the land panel's brush tab");
await ui.click("land_panel/Land_Brush_Height_Mode"); // The brushes open spraying; these strokes are dragged
await until(() => ui.find("land_panel/body/Land_Brush_Height_Mode_Drag") !== undefined, 60, "the mode tab");
await ui.click("land_panel/body/Land_Brush_Height_Mode_Drag");
await ui.click("land_panel/Land_Brush_Height_Raise");
await until(() => ui.find("land_panel/body/Land_Brush_Height_Raise_Hill") !== undefined, 60, "the hill brush");
await ui.click("land_panel/body/Land_Brush_Height_Raise_Hill");
for (let i = 0; i < 3; ++i) {
    const at = must(map.screen(hill.col, hill.row), `tile ${hill.col}, ${hill.row} in sight`);
    await pointer.drag([at.x, at.y], [at.x, at.y - 0.15], 40);
}
await land("Land_Brush_Height"); // Put away
await frames(5);
const top = () => Math.max(...map.tile(hill.col, hill.row)!.corners);
const raised = top();
log("hill", JSON.stringify(hill), "up to", raised);
const cutting = await pickDesign("tracks/coasters/Corkscrew/Corkscrew", "Corkscrew");
let dig: Spot | undefined;
for (let k = 0; k < 121 && dig === undefined; ++k) {
    const at = { col: hill.col + 2 * (k % 11) - 10, row: hill.row + 2 * Math.floor(k / 11) - 10 };
    await over(at);
    if (refusal() === "PlaceObject_IntersectsGround")
        dig = at;
}
expect(dig !== undefined, "a spot where the design passes into the hill");
log("into the ground:", status());
await screenshot("design_place_into_ground");
for (const [tab, lit] of [["Sharp", "TerrainFlattener_SharpOn"], ["Smooth", "TerrainFlattener_SmoothOn"]]) {
    await ui.click(`${kPanel}/TerrainFlattener_${tab}Off`);
    await frames(3);
    expect(ui.find(`${kPanel}/${lit}`) !== undefined && refusal() === "", `the ground cut ${tab.toLowerCase()}ly, the design free, not ${refusal()}`);
}
expect(ui.find(`${kPanel}/TerrainFlattener_SharpOff`) !== undefined, "one ground tab lit at a time");
await ui.click(`${kPanel}/TerrainFlattener_SmoothOn`);
await frames(3);
expect(refusal() === "PlaceObject_IntersectsGround", `the lit tab put out, ground refused again, not ${refusal()}`);
await ui.click(`${kPanel}/TerrainFlattener_SmoothOff`);
await frames(3);
await over(dig!);
expect(refusal() === "", `the design free with the ground cut smoothly, not ${refusal()}`);
await screenshot("design_place_adjust");
await lay();
expect((rides.list()[cutting].pieces ?? 0) > 0, "the design laid into the hill");
log("hill top", raised, "->", top());
camera.look({ col: hill.col, row: hill.row }, { col: hill.col + 12, row: hill.row + 22, up: 30 });
await frames(20);
await screenshot("design_place_cut");

// ── 4. Off the park's land, on a park that owns only some of its map ──
await park.open("Campaigns/Base/VanillaHills.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "tools");
const size = map.size();
await pickDesign("tracks/coasters/Corkscrew/Corkscrew", "Corkscrew");
let edge: Spot | undefined;
for (let k = 0; k < size.width * size.height && edge === undefined; k += 3) {
    const at = { col: k % size.width, row: Math.floor(k / size.width) };
    if (at.col >= 8 && at.row >= 8 && map.tile(at.col, at.row)?.owned === false && map.tile(at.col + 6, at.row + 6)?.owned === false)
        edge = at;
}
log("unowned", JSON.stringify(edge));
expect(edge !== undefined, "land the park does not own");
await over(edge!);
expect(refusal() === "PlaceObject_NotOwned", `the design refused off the park's land, not ${refusal()}`);
log("not owned:", status());
await screenshot("design_place_not_owned");
