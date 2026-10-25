// SPDX-License-Identifier: GPL-3.0-or-later
//
// Animal enclosures in a park of the Wild! expansion: the park keeps which tiles each enclosure takes, and its
// fences stand again round them when it loads.
//
//   ./nob script scripts/tests/enclosures.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
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
