// SPDX-License-Identifier: GPL-3.0-or-later
//
// Children and the rides not for them, on a saved park with such rides (motorbikes, stand-up, flying, lay-down
// coasters and the like) and ordinary ones. With the park running a while:
//
//   - no child rides one not for children, since their seats need a pose a child's body lacks;
//   - children still ride ordinary rides.
//
//   ./nob script scripts/tests/child_rides.ts -- --no-audio --park PATH

export {};

await until(() => park.loaded(), 1200, "the park");
await park.ready();

// Kinds whose items mark them not for children, by symbol.
const kNotForChildren =
    /^(MotorBikes|StandupCoaster|StandupTwister|Flying|LayDown|AirPoweredRC|Steeplechase|SuperSoaker|AlpineCoaster|QuadBikes|BoosterBikes|TowerCoaster|SpinningSteel|JetSkis|SwanBoats|Canoes|RowingBoats|SqueakyCycles|WaterTricycles|Windsurfer|SkySwinger|skycoaster|revolution|zipper|roundup):/;
const list = rides.list();
const barred = new Set(list.flatMap((r, i) => (kNotForChildren.test(r.symbol) && !r.demolished ? [i] : [])));
log("rides not for children:", [...barred].map((i) => list[i].symbol).join(", "));
expect(barred.size > 0, "the park has a ride not for children");

type Riding = WalkedPerson & { state: "riding" };
const riding = () => people.list().filter((p): p is Riding => (p as WalkedPerson).state === "riding");
let children = 0;
let adultsBarred = 0;
const ridden = new Set<number>();
for (let minute = 0; minute < 10; ++minute) {
    await park.advance(60);
    for (const p of riding()) {
        if (p.ride === undefined) continue;
        if (p.child) {
            expect(!barred.has(p.ride), `child ${p.name} on ${list[p.ride].symbol}`);
            ++children;
            ridden.add(p.ride);
        } else if (barred.has(p.ride)) ++adultsBarred;
    }
}
log("children seen riding", children, "on", [...ridden].map((i) => list[i].symbol).join(", "));
log("grown-ups seen on rides not for children", adultsBarred);
expect(children > 0, "children still ride ordinary rides");
expect(adultsBarred > 0, "grown-ups still ride those not for children");

// Riders on a ride not for children, all grown-ups.
await park.advanceUntil(
    () => riding().some((p) => p.ride !== undefined && barred.has(p.ride)),
    600,
    "a rider on a ride not for children",
);
const rider = must(
    riding().find((p) => p.ride !== undefined && barred.has(p.ride)),
    "a rider",
);
log("looking at", rider.name, "on", list[rider.ride!].symbol);
camera.look(
    { col: rider.col, row: rider.row, up: rider.up },
    { col: rider.col + 4, row: rider.row + 6, up: rider.up + 8 },
);
await frames(10);
await screenshot("child_rides_barred_ride");
