// SPDX-License-Identifier: GPL-3.0-or-later
//
// Under-Water Tunnel ends: a lone raised piece of the tunnel style is drawn closed all round, and a ramp laid on
// from it is closed at its far end while the flat behind it opens towards it.
//
//   ./nob script scripts/tests/tunnel_caps.ts -- --no-audio

export {};

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined, 1200, "the tool row");
const gate = map.paths().reduce((a, b) => (b.row > a.row ? b : a));
const col = gate.col + 3;
const row = gate.row + 4;

await ui.click("tools/Paths/Obj_Paths");
await until(() => ui.find("footpaths/route") !== undefined, 120, "the path panel");
await ui.toggle("footpaths/follow", false);
await ui.toggle("footpaths/route", false);
await ui.click("footpaths/paths");
await frames(2);
const styles = ui.children("footpaths/body") ?? [];
const tunnel = must(
    styles.find((s) => s.toLowerCase().includes("underwater")),
    `the Under-Water Tunnel style among ${styles}`,
);
await ui.click(`footpaths/body/${tunnel}`);
await frames(2);
const straight = must(under("footpaths", "Path_Straight"), "the Straight button");
const up = must(under("footpaths", "Path_UpwardSlope"), "the Upward Slope button");

// Lifted off the ground with Shift, as path_lift.ts does.
await look_down(col, row, 30);
const [x, y] = on_view(col, row);
pointer.move(x, y);
await frames(2);
pointer.shift(true);
await frames(2);
for (let i = 1; i <= 16; ++i) {
    pointer.move(x, y - 0.01 * i);
    await frames(1);
}
await frames(3);
const before = map.paths().length;
await pointer.click(x, y - 0.16);
await frames(3);
const first = map.paths().slice(before);
expect(first.length === 1 && first[0].raised, `one raised piece laid, not ${JSON.stringify(first)}`);
// From the side and close, so the ends show.
const close_up = async () => {
    camera.look({ col: col + 0.5, row: row + 1 }, { col: col + 3, row: row + 4, up: 24 });
    await frames(3);
};
const decks = () => map.objects().filter((o) => o.overlay.toLowerCase().includes("underwater"));
log("lone", JSON.stringify(decks()));
await close_up();
await screenshot("lone_flat");
expect(
    decks().some((o) => o.symbol.toLowerCase() === "waterflatfc"),
    `the lone piece closed all round, not ${JSON.stringify(decks())}`,
);

// Back to the view it was laid from, so the ramp goes on from it at its height.
await look_down(col, row, 30);
pointer.move(x, y - 0.16);
await frames(3);
await ui.click(up);
await frames(2);
await ui.click(straight);
await frames(3);
pointer.shift(false);
await frames(2);
const laid = map.paths().slice(before);
log("laid", JSON.stringify(laid), JSON.stringify(decks()));
expect(laid.length === 2, `a ramp laid on, not ${JSON.stringify(laid)}`);
await close_up();
await screenshot("ramp_end");
const symbols = decks().map((o) => o.symbol.toLowerCase());
expect(
    symbols.some((s) => s.startsWith("waterslope") && s.endsWith("tc")) && !symbols.includes("waterflatfc"),
    `the ramp closed at its top and the flat open towards it, not ${symbols}`,
);
quit(0);
