// SPDX-License-Identifier: GPL-3.0-or-later
//
// A pool complex with a water slide: swimmers go up to its station, ride it down in their swimwear and splash
// into the pool, where they swim on. A car running off the slide's open end into the water is no crash: no
// wreck, no message, the slide stays open, rated, and its cars go round again. Give it a park the game saved
// with a body, ring or pro slide ending in a pool and guests swimming:
//
//   ./nob script scripts/tests/pool_slides.ts -- --no-intro --no-audio --seed 1 --park PARK

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
const slide = rides.list().findIndex((r) => !r.flat && !r.demolished && /(body|ring|pro)slide/i.test(r.symbol));
expect(slide >= 0, "a pool slide in the park");
const info = () => rides.list()[slide];
log("slide", info().name, info().symbol, "trains", info().trains, "length", info().trackLength?.toFixed(1));
expect(info().joined, "the slide run by the park");
expect(info().ratings !== undefined, "the slide rated");
const messages = park.messages().length;

// A swimmer riding it, in their swimwear: a look at them on the way down.
const riding = () => people.list().findIndex((p) => p.state === "riding" && p.ride === slide);
await until(() => riding() >= 0 && (info().trainsAt ?? []).some((t) => t.station === null && t.riders > 0), 6000, "a swimmer riding the slide");
await until(() => riding() >= 0 && people.list()[riding()].reaction !== "waiting", 600, "the rider on their way down");
const rider = riding();
expect(people.list()[rider].swimwear === true, "the rider in their swimwear");
const at = people.list()[rider];
camera.look({ col: at.col!, row: at.row!, up: at.up! }, { col: at.col! + 3, row: at.row! + 3, up: at.up! + 3 });
await frames(6);
await screenshot("pool_slides_riding");

// Off the end they splash down into the pool and swim on, still in their swimwear.
await until(() => people.list()[rider].state !== "riding", 1200, "the rider off the end");
const flying = people.list()[rider];
log("off the end as", flying.state, "swimwear", flying.swimwear);
expect(flying.swimwear === true, "the rider still in their swimwear off the end");
camera.look({ col: flying.col!, row: flying.row!, up: flying.up! }, { col: flying.col! + 4, row: flying.row! + 4, up: flying.up! + 3 });
await frames(2);
await screenshot("pool_slides_splashdown");
await until(() => people.list()[rider].state === "swimming", 600, "the rider swimming again");
expect(people.list()[rider].swimwear === true, "the rider swimming in their swimwear");
await frames(30);
await screenshot("pool_slides_swimming");

// Cars go off the end again and again, and none of it is a crash.
await until(() => (info().customers ?? 0) >= 3, 6000, "three swimmers down the slide");
expect((info().crashes ?? 0) === 0, `no crash, not ${info().crashes}`);
expect((info().wrecks ?? []).length === 0, "no wrecks");
expect(info().open === true, "the slide still open");
const told = park.messages().slice(messages).map((m) => m.key);
expect(!told.some((k) => /crash/i.test(k)), `no crash message, not ${told.join(" ")}`);
const people_now = people.list();
expect(people_now.every((p) => p.state !== "thrown"), "nobody thrown");
log("customers", info().customers, "riders now", info().riders);
