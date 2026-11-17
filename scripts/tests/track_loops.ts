// SPDX-License-Identifier: GPL-3.0-or-later
//
// Builds a vertical loop by hand on looping coasters, each on a new blank landscape: a station, two level straights,
// then a gentle climb, as the loop's pieces start climbing gently and end falling gently. On the level straight no
// loop can be built; after the climb one is, and the track eases back to level after it.
//
//   ./nob script scripts/tests/track_loops.ts -- --no-audio

export {};

const loop = /^(inv)?loop(left|right)\b/i; // A track names its pieces with their tag, ":tks"

/** Picks the piece the construction panel lists with a name matching `name`, going down the list from its first
 * row, and builds it with Enter when `build_it`; false when no row the list offers has it. */
async function pick_listed(name: RegExp, build_it: boolean): Promise<boolean> {
    const list = ui.find("placing/body/construction/list");
    if (list === undefined) return false;
    // A second press on the picked row soon after the last builds its piece, so the last press is waited out first.
    const since = Date.now();
    while (Date.now() - since < 500) await frames(1);
    await pointer.click(list.x, list.top + 0.03);
    await frames(2);
    keys.press("Home");
    await frames(2);
    for (let row = 0; row < 80 && !name.test(build.construction()?.piece ?? ""); ++row) {
        keys.press("Down");
        await frames(2);
    }
    if (!name.test(build.construction()?.piece ?? "")) return false;
    if (build_it) {
        keys.press("Enter");
        await frames(10);
    }
    return true;
}

async function loop_after_climb(kind: string): Promise<void> {
    await park.open("Campaigns/Base/BlankLandscape.dat");
    await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "tools");
    expect(build.pick(`tracks/coasters/${kind}/${kind}`, kind, "designs"), `a ${kind} coaster to build`);
    await until(() => ui.find("design_browser") !== undefined, 600, "the designs window");
    await frames(3);
    await ui.click("design_browser/content/build");
    await frames(10);
    const ride = build.construction()!.ride;
    const { width, height } = map.size();
    const at = { col: Math.floor(width / 2) - 10, row: Math.floor(height / 2) };
    camera.look({ col: at.col + 6, row: at.row + 0.5 }, { col: at.col + 6, row: at.row + 24, up: 22 });
    await frames(5);
    const p = must(map.screen(at.col, at.row), `tile ${at.col}, ${at.row} in sight`);
    pointer.move(p.x, p.y);
    await frames(10);
    await pointer.click(p.x, p.y);
    await frames(30);
    const laid = () => tracked(rides.list()[ride], "the coaster").track;
    expect(laid().length > 0, `${kind}: the station laid`);
    const button = async (name: string): Promise<boolean> => {
        const before = laid().length;
        const path = `placing/body/construction/AttractionEditor_Icon${name}`;
        if (ui.find(path) === undefined) return false;
        for (let tries = 0; tries < 3 && laid().length === before; ++tries) {
            await ui.click(path);
            await frames(10);
        }
        return laid().length > before;
    };
    for (let i = 0; i < 2; ++i) expect(await button("Curve0"), `${kind}: straight ${i} laid`);
    expect(
        !(await pick_listed(loop, false)) || build.construction()?.ghost !== true,
        `${kind}: no loop to build on the level, not ${build.construction()?.piece}`,
    );
    // A slope picked, then the straight button lays it.
    await ui.click("placing/body/construction/AttractionEditor_IconSlopeU1");
    await frames(3);
    expect(await button("Curve0"), `${kind}: a gentle climb laid`);
    expect(await pick_listed(loop, false), `${kind}: a loop offered after the climb, on ${JSON.stringify(laid())}`);
    expect(build.construction()?.ghost === true && build.construction()?.clashes === false, `${kind}: the loop clear`);
    expect(await pick_listed(loop, true), `${kind}: the loop built`);
    expect(loop.test(laid().at(-1) ?? ""), `${kind}: the loop laid last, not ${JSON.stringify(laid())}`);
    log(`${kind}:`, laid().join(" "));
    await screenshot(`${kind}_loop`);
    await ui.click("placing/body/construction/AttractionEditor_IconSlope0");
    await frames(3);
    expect(await button("Curve0"), `${kind}: level again after the loop's drop`);
    build.stop();
}

// The looping coaster, and kinds on other track sets with loops.
for (const kind of ["LoopingRC", "Corkscrew", "WoodenRC", "StandupCoaster"]) await loop_after_climb(kind);
