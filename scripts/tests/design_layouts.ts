// SPDX-License-Identifier: GPL-3.0-or-later
//
// Lays saved designs with pieces whose ends do not quite meet as their shapes are read, a heartline coaster, a
// log flume and a rapids ride, and a looping coaster whose first piece is above its lowest, each on the game's
// blank landscape. Each is laid whole with its doors, none of it under the ground, closed round so it can be
// tested, and looked at from above.
//
//   ./nob script scripts/tests/design_layouts.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

// The ride's overlay, its kind, the design's place among its kind's designs and how many pieces it has.
const designs: [string, string, number, number, string][] = [
    ["tracks/coasters/Heartline/Heartline", "Heartline", 0, 44, "heartline"],
    ["tracks/TrackedRides/LogFlume/LogFlume", "LogFlume", 0, 63, "log_flume"],
    ["tracks/TrackedRides/Rapids/Rapids", "Rapids", 0, 50, "rapids"],
    ["tracks/coasters/LoopingRC/LoopingRC", "LoopingRC", 1, 110, "looping"],
];

for (const [overlay, kind, place, pieces, name] of designs) {
    await park.open("Campaigns/Base/BlankLandscape.dat");
    await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "tools");
    const ground = Math.min(...map.tile(64, 64)!.corners);
    expect(build.pick(overlay, kind, "designs"), `a ${kind} to build`);
    await until(() => ui.find("design_browser") !== undefined, 600, "the designs window");
    await frames(3);
    // Its designs come a heading and the custom track below the top, in the window's units (a button is 22 of
    // them high).
    const list = ui.find("design_browser/content/list")!;
    const button = ui.find("design_browser/content/build")!;
    const unit = (button.bottom - button.top) / 22;
    await pointer.click(list.x, list.top + (18 + 16 * (2 + place)) * unit);
    await frames(5);
    await ui.click("design_browser/content/build");
    await frames(10);
    const ride = build.construction()!.ride;
    // Somewhere clear of the park's edge.
    let laid = false;
    for (let i = 0; i < 30 && !laid; ++i) {
        const at: [number, number] = [0.3 + 0.08 * (i % 6), 0.3 + 0.1 * Math.floor(i / 6)];
        pointer.move(...at);
        await frames(20);
        const c = build.construction()!;
        if (!c.ghost || c.clashes)
            continue;
        await pointer.click(...at);
        await frames(30);
        laid = (rides.list()[ride].pieces ?? 0) > 0;
    }
    const r = rides.list()[ride];
    expect(r.pieces === pieces, `the ${kind} design laid whole, ${pieces} pieces, not ${r.pieces}`);
    const heights = map.objects().filter((o) => o.overlay.toLowerCase().startsWith("tracks/") && o.height !== null).map((o) => o.height!);
    expect(Math.min(...heights) >= ground, `the ${kind} design above the ground, its lowest piece at ${Math.min(...heights)}, not below ${ground}`);
    build.stop();
    await ui.click("placing/close");
    await frames(5);
    expect(rides.list()[ride].entrance !== null, `the ${kind} design laid with its doors`);
    expect(rides.setState(ride, "testing") == null, `the ${kind} design closed round, to be tested`);
    const site = rides.list()[ride].entrance!;
    camera.look({ col: site.col + 0.5, row: site.row + 0.5 }, { col: site.col + 0.5, row: site.row + 30.5, up: 60 });
    await frames(90);
    await screenshot(name);
}
