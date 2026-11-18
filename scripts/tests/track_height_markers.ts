// SPDX-License-Identifier: GPL-3.0-or-later
//
// The construction panel's height button on a looping coaster on the game's blank landscape: off at first, it shows
// each end of each piece's height over the ground with a tick down to it, and the ghost's too. A station, two
// straights and two gentle climbs are laid, then the camera looks along the rise. The button stays on when the
// building is closed and opened again.
//
//   ./nob script scripts/tests/track_height_markers.ts -- --no-audio

export {};

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "tools");
expect(build.pick("tracks/coasters/LoopingRC/LoopingRC", "LoopingRC", "designs"), "a looping coaster to build");
await until(() => ui.find("design_browser") !== undefined, 600, "the designs window");
await frames(3);
await ui.click("design_browser/content/build");
await frames(10);
const ride = build.construction()!.ride;
const { width, height } = map.size();
const at = { col: Math.floor(width / 2) - 10, row: Math.floor(height / 2) };
camera.look({ col: at.col + 6, row: at.row + 0.5 }, { col: at.col + 6, row: at.row + 24, up: 22 });
await frames(5);
const p = must(map.screen(at.col, at.row), "the first tile in sight");
pointer.move(p.x, p.y);
await frames(10);
await pointer.click(p.x, p.y);
await frames(30);
const laid = () => tracked(rides.list()[ride], "the coaster").track;
expect(laid().length > 0, "the station laid");

const lay = async (name: string) => {
    const before = laid().length;
    for (let tries = 0; tries < 3 && laid().length === before; ++tries) {
        await ui.click(`placing/body/construction/AttractionEditor_Icon${name}`);
        await frames(10);
    }
    expect(laid().length > before, `${name} laid`);
};
await lay("Curve0");
await lay("Curve0");

expect(build.construction()?.heights === false, "the height markers off at first");
await ui.click("placing/body/construction/heights");
await frames(3);
expect(build.construction()?.heights === true, "the height markers on");
await ui.click("placing/body/construction/AttractionEditor_IconSlopeU1");
await frames(3);
await lay("Curve0");
await lay("Curve0");
camera.look({ col: at.col + 6, row: at.row + 0.5, up: 3 }, { col: at.col + 6, row: at.row + 22, up: 14 });
await frames(10);
await screenshot("height_markers"); // Heights at each piece's ends, ticks to the ground, and the ghost's

build.stop();
await frames(5);
rides.show(ride);
await frames(5);
await ui.click("info/Attraction_Construction");
await until(() => build.construction() !== undefined, 120, "the building open again");
expect(build.construction()?.heights === true, "the height markers still on after opening again");
