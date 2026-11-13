// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pool paths beside a pool slide's station. A path square takes its shape from everything meeting it, the station's
// platform too: the saved path square beside the station is a corner, as the game saved it. Pressed on the platform,
// pinned, the paths page starts a trail off its side at its deck; unpinned, a drag begun on the platform starts
// beside it at its deck too. The squares the trail lays beside the station join it: a T where the path goes on,
// an L at its end. Give it pool-7-slide, whose slide station runs along column 83, rows 24 to 26, at height 16,
// with a pool path at (84, 24) coming down a slope from the north:
//
//   ./nob script scripts/tests/pool_path_joins.ts -- --no-intro --no-audio --no-simulate --seed 1 --park PARK

export {};

const kPool = "Pool/Default/Default_Stub";
const kDeck = 16;

await until(() => park.loaded(), 1200, "the park");
await frames(10);
park.setWeather("sunny");
park.setTime(0.5);

const path_at = (col: number, row: number) => map.objects().find((o) => o.overlay === kPool && o.symbol.startsWith("Path") && o.col === col && o.row === row);
const shape = (col: number, row: number) => {
    const o = path_at(col, row);
    return o ? `${o.symbol} ${o.turn} @${o.height}` : "none";
};
const slide = rides.list().findIndex((r) => !r.demolished && /(body|ring|pro)slide/i.test(r.symbol));
expect(slide >= 0, "a pool slide in the park");
const decks = rides.list()[slide].platformSquares ?? [];
log("platform squares", JSON.stringify(decks));
expect([24, 25, 26].every((row) => decks.some((d) => d.col === 83 && d.row === row && d.height === kDeck)), "the slide's platform along column 83 at height 16");

// The saved square beside the station: the slope to the north and the station to the west meet it, a corner.
log("(84,24)", shape(84, 24));
expect(shape(84, 24) === `PathCorner 2 @${kDeck}`, `a corner joining the slope and the station at (84,24), not ${shape(84, 24)}`);

// The camera keeps its target on the ground: it looks past the station from above so its deck is in the middle.
const look = () => camera.look({ col: 78, row: 25.5 }, { col: 88, row: 27, up: 30 });
const screen = (col: number, row: number): [number, number] => {
    const at = map.screen(col, row, kDeck);
    if (!at)
        throw new Error(`${col}, ${row} is not in sight`);
    return [at.x, at.y];
};
look();
await frames(30);
await screenshot("pool_path_joins_saved");

// The paths page, from the slide's window.
rides.show(slide);
await frames(10);
await ui.click("info/body/Pool_PathPanel");
await until(() => ui.find("pool_panel/Pool_PathPanel")?.on === true, 120, "the pool panel on its paths page");
look();
await frames(30);

// Pinned: a press near the platform's east side starts a trail there at its deck; the controls lay the first piece.
await ui.toggle("pool_panel/pin", true);
await ui.click("pool_panel/body/paths/Path_NoSlope");
pointer.move(...screen(83.85, 25.5));
await frames(3);
const shown = build.pooling();
log("at the platform", JSON.stringify(shown));
expect(shown?.ghost?.startsWith("Path") === true && shown.ghostAt?.col === 84 && shown.ghostAt?.row === 25 && shown.ghostAt?.height === kDeck, `the first piece's ghost off the platform at its deck, not ${JSON.stringify(shown)}`);
await pointer.click(...screen(83.85, 25.5));
await frames(3);
await ui.click("pool_panel/body/paths/Path_Straight");
await frames(5);
log("pinned trail", shape(84, 25));
expect(path_at(84, 25)?.height === kDeck, `a path laid off the platform at its deck, not ${shape(84, 25)}`);

// Unpinned: a drag begun on the platform starts beside it at its deck, not on the platform.
await ui.toggle("pool_panel/pin", false);
await pointer.drag(screen(83.85, 26.5), screen(83.85, 26.5), 4);
await frames(5);
log("drag from the platform", shape(84, 26), "on the platform", shape(83, 26));
expect(path_at(84, 26)?.height === kDeck && path_at(83, 26) === undefined, `a path laid off the platform at its deck, not ${shape(84, 26)} and ${shape(83, 26)}`);
build.stop();
await frames(5);

// The station joins each square beside it: Ts where the path goes on past it, an L at its end. The T's closed side,
// the east, is the side nothing meets.
log("shapes", shape(84, 24), shape(84, 25), shape(84, 26));
expect(shape(84, 24) === `PathTJunction 0 @${kDeck}`, `a T at (84,24), not ${shape(84, 24)}`);
expect(shape(84, 25) === `PathTJunction 0 @${kDeck}`, `a T at (84,25), not ${shape(84, 25)}`);
expect(shape(84, 26) === `PathCorner 2 @${kDeck}`, `an L at (84,26), not ${shape(84, 26)}`);
look();
await frames(20);
await screenshot("pool_path_joins_station");
camera.look({ col: 78, row: 24 }, { col: 88, row: 20, up: 24 });
await frames(20);
await screenshot("pool_path_joins_station_side");
