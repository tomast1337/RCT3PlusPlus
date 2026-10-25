// SPDX-License-Identifier: GPL-3.0-or-later
//
// Animal enclosures. In a park of the Wild! expansion the park keeps which tiles each enclosure takes, and its
// fences stand again round them when it loads. There, a drag with a fence from the enclosure
// list fences the tiles round, paid for; a drag with the list's delete tool takes some of them out again.
//
//   ./nob script scripts/tests/enclosures.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

/** Where the middle of tile (col, row) is on the view; throws when it is out of sight. */
function on_view(col: number, row: number): [number, number] {
    const at = map.screen(col, row);
    if (!at)
        throw new Error(`tile ${col}, ${row} is not in sight`);
    return [at.x, at.y];
}

await park.open("Campaigns/Base/Wild/ScrubGardens.dat");
await until(() => ui.find("tools/Land/Land") !== undefined, 600, "the tool row");
const panels = map.objects().filter((o) => o.overlay.startsWith("Enclosures/Fences/"));
log(`${panels.length} enclosure fence panels`);
expect(panels.length > 100, `the enclosures fenced, not ${panels.length} panels`);
const mid = panels[Math.floor(panels.length / 2)];
camera.look({ col: mid.col + 0.5, row: mid.row + 0.5 }, { col: mid.col + 12, row: mid.row + 14, up: 18 });
await frames(30);
await screenshot("scrub_gardens_fences");

const fenced = () => map.objects().filter((o) => o.overlay.startsWith("Enclosures/Fences/")).length;

// A five by four rectangle of the park's own land, a tile clear of the enclosures and of paths.
const { width, height } = map.size();
const paths = map.paths();
const clear = (c: number, r: number) => {
    for (let row = r - 1; row <= r + 4; ++row)
        for (let col = c - 1; col <= c + 5; ++col)
        {
            const tile = map.tile(col, row);
            if (!tile?.owned || tile.enclosure >= 0 || paths.some((p) => p.col === col && p.row === row))
                return false;
        }
    return true;
};
let site: { col: number; row: number } | undefined;
for (let r = 1; r < height - 5 && !site; r += 2)
    for (let c = 1; c < width - 6 && !site; c += 2)
        if (clear(c, r))
            site = { col: c, row: r };
expect(site !== undefined, "room for an enclosure");
const c0 = site!.col;
const r0 = site!.row;
camera.look({ col: c0 + 2.5, row: r0 + 2 }, { col: c0 + 2.5, row: r0 + 14, up: 20 });
await frames(10);
const cash = park.cash()!;
expect(build.pick("Enclosures/Fences/Wood/Wood_Stub", "Wood"), "the wooden fence in the enclosure list");
await pointer.drag(on_view(c0, r0), on_view(c0 + 4, r0 + 3), 8);
await frames(3);
expect(fenced() === panels.length + 18, `a five by four enclosure fenced, not ${fenced() - panels.length} panels`);
log("fence paid", cash - park.cash()!);
expect(park.cash()! < cash, "the fence paid for");
await screenshot("enclosure_built");
expect(build.pick("", "EnclosureDelete"), "the enclosure delete tool");
await pointer.drag(on_view(c0 + 3, r0), on_view(c0 + 4, r0 + 3), 6);
await frames(3);
expect(fenced() === panels.length + 14, `three by four left, not ${fenced() - panels.length} panels`);
build.stop();
await frames(60);
await screenshot("enclosure_cut");
