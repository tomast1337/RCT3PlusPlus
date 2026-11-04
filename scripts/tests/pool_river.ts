// SPDX-License-Identifier: GPL-3.0-or-later
//
// A lazy river joined to pool complexes by pool path stairs at its stations: swimmers float round it in rings,
// carried by the current, treading water or paddling, free on their complex's ticket, and get off up the stairs
// at a station to swim on. The build menu offers the river among the pool rides. Give it a park the game saved
// with a lazy river and guests swimming:
//
//   ./nob script scripts/tests/pool_river.ts -- --no-intro --no-audio --seed 1 --park PARK

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

await until(() => park.loaded(), 1200, "the park");
await frames(10);
park.setWeather("sunny");
park.setTemperature(28);
park.setTime(0.5);
const river = rides.list().findIndex((r) => !r.flat && !r.demolished && /lazyriver/i.test(r.symbol));
expect(river >= 0, "a lazy river in the park");
const info = () => rides.list()[river];
const complaints = () => park.messages().filter((m) => /NoPathToEntrance/.test(m.key) && (m.name === info().name || /lazy river/i.test(m.name))).length;
const saved = complaints();
log("river", info().name, info().symbol, "length", info().trackLength?.toFixed(1), "stations", info().riverStations, "joined", info().riverJoined, "ratings", JSON.stringify(info().ratings));
expect(info().joined, "the river run by the park");
expect(info().price === 0, `the river free, not ${info().price}`);
expect((info().riverStations ?? 0) > 0, "the river's stations");
// A station with no pool path stairs of a complex beside it lets nobody on or off.
await until(() => (info().riverJoined ?? 0) > 0, 30, "a station joined to a complex");
log("joined", info().riverJoined, "of", info().riverStations);

// Riders the park kept float on round it, in their swimwear.
const floating = () => people.list().filter((p) => p.floating !== undefined && p.ride === river);
await until(() => floating().length > 0, 60, "riders on the river");
log("floating now", floating().length);
expect(floating().every((p) => p.swimwear === true), "the riders in their swimwear");
const one = floating()[0];
camera.look({ col: one.col!, row: one.row!, up: one.up! }, { col: one.col! + 3, row: one.row! + 3, up: one.up! + 10 });
await frames(30);
await screenshot("pool_river_floating");
// Close by, sitting in their rings.
camera.look({ col: one.col!, row: one.row!, up: one.up! }, { col: one.col! + 0.15, row: one.row! + 0.15, up: one.up! + 3 });
await frames(30);
await screenshot("pool_river_rings");

// Swimmers get on from the pools, free, and those far enough round get off up the stairs to swim on.
const customers = info().customers ?? 0;
await until(() => (info().customers ?? 0) > customers, 6000, "a swimmer getting on");
const names = new Set(floating().map((p) => p.name));
await until(() => people.list().some((p) => names.has(p.name) && p.floating === undefined && p.state === "swimming"), 6000, "a rider off into a pool");
const off = people.list().find((p) => names.has(p.name) && p.floating === undefined && p.state === "swimming")!;
expect(off.swimwear === true, "the rider off still in their swimwear");
expect(people.list().some((p) => p.floating === "paddling") || people.list().some((p) => p.floating === "resting"), "riders resting or paddling");
camera.look({ col: off.col!, row: off.row!, up: off.up! }, { col: off.col! + 4, row: off.row! + 4, up: off.up! + 4 });
await frames(10);
await screenshot("pool_river_off");
log("customers", info().customers, "riders now", info().riders);
// It has no entrance to complain of (the park may have kept such a message from before).
expect(complaints() === saved, `no new complaint about the river's entrance, not ${complaints() - saved}`);

// The river among the pool rides in the build menu.
await until(() => build.flatRides() !== undefined, 600, "the build lists");
expect(build.pick("tracks/TrackedRides/LazyRiver/LazyRiver", "LazyRiver"), "the lazy river in the build menu");
await frames(10);
build.stop();
