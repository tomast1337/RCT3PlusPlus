// SPDX-License-Identifier: GPL-3.0-or-later
//
// The water running down a water ride's track (log flumes, splash boats, rapids, water coasters, slides). It
// flows the way the track runs on every piece, right-hand curves and pieces laid the other way round
// included. Pictures a right-hand curve close up twice, a quarter of a second apart, so the water can be seen to
// move on downhill, and a car splashing through water. Give it a park the game saved with an open water ride
// running, such as a log flume:
//
//   ./nob script scripts/tests/ride_water.ts -- --no-intro --no-audio --seed 1 --park PARK

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

const kWet = /flume|splash|rapid|watercoaster|dingy|slide/i;

await until(() => park.loaded(), 1200, "the park");
await frames(10);
const wet = rides.list().filter((r) => kWet.test(r.overlay) || kWet.test(r.symbol));
expect(wet.length > 0, "a water ride in the park");
log("water rides", wet.map((r) => r.symbol).join(", "));
// Open pieces of ride track turning right: tubes hide their water.
const pieces = map.objects().filter((o) => /trackedrides/i.test(o.overlay) && /curve.*right/i.test(o.symbol) && !/capped/i.test(o.symbol));
const curve = pieces.find((o) => /slope/i.test(o.symbol)) ?? pieces[0];
expect(curve !== undefined, "a piece of water ride track");
log("piece", curve.symbol, curve.col, curve.row, curve.height);
camera.look({ col: curve.col + 0.5, row: curve.row + 0.5, up: 1 }, { col: curve.col + 2.5, row: curve.row + 2, up: 7 });
await frames(30);
await screenshot("ride_water_curve");
await frames(15);
await screenshot("ride_water_curve_later");

// A car splashing through the water sprays from its splash points: pictured close up.
const kSprays = /^(splash2|splash4|watersplashbig)$/;
await until(() => map.effects().some((e) => kSprays.test(e.effect)), 60 * 120, "a car splashing through water");
const spray = map.effects().find((e) => kSprays.test(e.effect))!;
log("spray", spray.effect, spray.col, spray.row, spray.up);
camera.look({ col: spray.col, row: spray.row, up: spray.up }, { col: spray.col + 3, row: spray.row + 2.5, up: spray.up + 4 });
await frames(6);
await screenshot("ride_water_splash");
