// SPDX-License-Identifier: GPL-3.0-or-later
//
// Places jumping fountains, a water cannon, a view point and a loudspeaker along the road in from the blank landscape's
// entrance, brings guests in, and waits to see each used: someone firing the cannon, someone turning to look at
// the fountains, a party stopped together at the view, someone liking the loudspeaker's music. Each is kept as a
// screenshot, the last in the guest's window, naming the speaker.
//
//   ./nob script scripts/tests/park_attractions.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "the build lists");
const paths = map.paths();
const gate = paths.reduce((a, b) => (b.row > a.row ? b : a));
const road = paths.filter((p) => p.col === gate.col && p.row < gate.row - 1).sort((a, b) => b.row - a.row);
const items = [
    { overlay: "Style/Vanilla/Style", symbol: "PathFountain", kind: "fountain", tile: road[1] },
    { overlay: "Style/Vanilla/CapeTyphoon/Style", symbol: "WaterCannon", kind: "water cannon", tile: road[4] },
    { overlay: "Style/Vanilla/Style", symbol: "VistaPoint", kind: "photo spot", tile: road[7] },
    { overlay: "Style/Vanilla/Style", symbol: "LoudSpeaker", kind: "speaker", tile: road[10] },
];
for (const item of items) {
    expect(item.tile !== undefined, `road for the ${item.symbol}`);
    camera.look({ col: item.tile.col + 0.5, row: item.tile.row + 0.5 }, { col: item.tile.col + 0.5, row: item.tile.row + 8.5, up: 25 });
    await frames(3);
    expect(build.pick(item.overlay, item.symbol), `${item.symbol} in the scenery lists`);
    await frames(2);
    const at = map.screen(item.tile.col, item.tile.row);
    expect(at !== undefined, `the ${item.symbol}'s tile in sight`);
    pointer.move(at!.x, at!.y);
    await frames(3);
    expect(build.placing()?.problem === "none", `${item.symbol} placeable, not ${build.placing()?.problem}`);
    await pointer.click(at!.x, at!.y);
    build.stop();
    await frames(1);
}
const kinds = park.pathItems().map((i) => i.kind);
log("path items", kinds.join(", "));
for (const item of items)
    expect(kinds.includes(item.kind as PathItemKind), `the ${item.symbol} known to guests as a ${item.kind}`);

park.dropParties(40, 3);
/** Waits for a guest `found` picks out, then looks at them and keeps a screenshot named `name`. */
async function show(name: string, found: () => { col?: number; row?: number } | undefined): Promise<void> {
    await park.advanceUntil(() => found() !== undefined, 1800, name);
    const who = found()!;
    camera.look({ col: who.col!, row: who.row! }, { col: who.col! + 1.5, row: who.row! + 2, up: 1.5 });
    await frames(4);
    await screenshot(name);
}
// The cannon a few seconds on, once they have stepped behind it and taken hold, close up from its side.
await park.advanceUntil(() => people.list().some((p) => p.lingering === "water cannon"), 1800, "someone firing the cannon");
await park.advance(5);
const cannon = road[4];
camera.look({ col: cannon.col + 0.5, row: cannon.row + 0.5 }, { col: cannon.col + 3, row: cannon.row + 1.5, up: 1.2 });
await frames(4);
await screenshot("cannon");
await show("fountain", () => people.list().find((p) => p.gesture === "turnlook" && p.col !== undefined));
await show("vista", () => {
    const party = people.list().filter((p) => p.lingering === "photo spot" && p.party !== undefined && p.col !== undefined);
    return party.find((p) => party.filter((q) => q.party === p.party).length >= 2);
});

const kNiceMusic = 29;
const fan = () => people.list().findIndex((p) => !p.staff && (p.thoughts ?? []).includes(kNiceMusic));
await park.advanceUntil(() => fan() >= 0, 1800, "a guest liking the speaker's music");
people.show(fan());
await until(() => ui.find("info/Guest_Thoughts") !== undefined, 120, "the guest's window");
await ui.click("info/Guest_Thoughts");
await frames(5);
await screenshot("speaker");
await ui.click("info/close");
