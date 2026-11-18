// SPDX-License-Identifier: GPL-3.0-or-later
//
// A test train that cannot climb: a looping coaster begun by hand on the game's blank landscape, a station, two
// level straights and a climb with no lift on it, tested while it is built. Its see-through train stalls on the
// climb, and a message says so once, however long the test goes on; the message's camera looks at where it stalled.
//
//   ./nob script scripts/tests/test_stall.ts -- --no-audio

export {};

const stalled = () => park.messages().filter((m) => m.key === "RideTestStalled");

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
const p = must(map.screen(at.col, at.row), `tile ${at.col}, ${at.row} in sight`);
pointer.move(p.x, p.y);
await frames(10);
await pointer.click(p.x, p.y);
await frames(30);
const laid = () => tracked(rides.list()[ride], "the coaster").track;
// A station of three pieces, long enough for the train: its picture button clicked once to pick the piece and
// again to build it.
for (let tries = 0; tries < 12 && laid().length < 3; ++tries) {
    for (let i = 0; i < 8; ++i) {
        const special = `placing/body/construction/special${i}`;
        if (ui.find(special) === undefined) continue;
        const before = laid().length;
        await ui.click(special);
        await frames(5);
        if (laid().length > before) break;
        if (/^station/i.test(build.construction()?.piece ?? "")) {
            await ui.click(special);
            await frames(10);
            break;
        }
    }
}
expect(laid().length >= 3, `a station of three, not ${JSON.stringify(laid())}`);
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
for (let i = 0; i < 2; ++i) expect(await button("Curve0"), `straight ${i} laid`);
// The climb steepens a step at a time: each slope picked, then the straight button lays it, the steepest piece after piece.
for (const [slope, count] of [
    ["SlopeU1", 1],
    ["SlopeU2", 1],
    ["SlopeU3", 3],
] as const) {
    expect(ui.find(`placing/body/construction/AttractionEditor_Icon${slope}`) !== undefined, `${slope} to pick`);
    await ui.click(`placing/body/construction/AttractionEditor_Icon${slope}`);
    await frames(3);
    for (let i = 0; i < count; ++i) expect(await button("Curve0"), `${slope} piece ${i} laid`);
}
log(laid().join(" "));
// The panel stays open: its ghost train is the one tested.
expect(rides.setState(ride, "testing") == null, "the unfinished track tested");
await frames(10);
expect(build.construction()?.testing === true, "a test train running the unfinished track");
await until(() => stalled().length > 0, 1800, "a stall told");
expect(stalled().length === 1, `one message, not ${stalled().length}`);
await frames(900); // Long enough for the train to crash off the end and set off again
expect(stalled().length === 1, `still one message after a long test, not ${stalled().length}`);
await screenshot("stalled");

// Its camera, clicked in the drawer, looks at the place.
const drawer = must(ui.find("messages"), "messages shown");
for (let tries = 0; tries < 3 && must(ui.find("messages"), "messages").bottom - drawer.top < 100; ++tries) {
    await pointer.click(drawer.x, drawer.y);
    await pointer.click(drawer.x, drawer.y);
    await frames(30);
}
const open = must(ui.find("messages"), "messages shown");
await pointer.click(
    open.left + ((open.right - open.left) * 27) / 480,
    open.bottom - ((open.bottom - open.top) * 35) / 360,
);
await frames(30);
await screenshot("stalled_camera");
quit(0);
