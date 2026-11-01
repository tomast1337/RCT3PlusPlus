// SPDX-License-Identifier: GPL-3.0-or-later
//
// On Vanilla Hills' suspended coaster, two trains of several cars run with guests on board, a block apart. Each
// train coming into the station lets the riders of every car off as soon as it stops, even while the block ahead
// still holds the other train, and guests board it there. One held up by the train ahead inside the station, short
// of its end, would let them off where it stands. Pictured as riders leave every car.
//
//   ./nob script scripts/tests/train_unloading.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

await park.open("Campaigns/Base/VanillaHills.dat");
await park.ready();
await frames(30);
const coaster = rides.list().findIndex((r) => r.symbol === "SuspendedSwinging:trr");
expect(coaster >= 0, "the suspended coaster in the park");
const info = rides.list()[coaster];
log("coaster", info.trains, "trains of", info.passengerCars, "cars, seats", info.capacity);
expect((info.trains ?? 0) >= 2 && (info.passengerCars ?? 0) >= 2 && info.open === true, "an open coaster with two trains of several cars");

const kStep = 0.25; // Park seconds between looks
const kOffWithin = 0.5; // Park seconds a stopped train's riders may take to be off
const stopped: ({ since: number; riders: number } | undefined)[] = []; // Since when each train has stood in a station with riders, and how many
let heldOff = 0;
let endOff = 0;
let boarded = 0;
let pictured = false;
let last = rides.list()[coaster].trainsAt!;
for (let k = 0; k < 300 / kStep && endOff < 3; ++k) {
    await park.advance(kStep);
    const now = rides.list()[coaster].trainsAt!;
    for (let t = 0; t < now.length; ++t) {
        const train = now[t];
        const was = last[t];
        const still = train.station !== null || train.held !== null;
        const came = was.station === null && was.held === null; // Under way the look before
        if (still && came && was.riders > 0 && stopped[t] === undefined)
            stopped[t] = { since: k * kStep, riders: was.riders };
        // Off, though those boarding next may already be sitting down, one at a time.
        if (stopped[t] !== undefined && train.riders < stopped[t]!.riders) {
            log(`train ${t} riders off ${(k * kStep - stopped[t]!.since).toFixed(2)} s after stopping`, train.held !== null ? `held ${train.front.toFixed(1)} m along` : "at the end");
            if (train.held !== null)
                ++heldOff;
            else
                ++endOff;
            if (!pictured) {
                const cars = rides.list()[coaster].cars!;
                const per = rides.list()[coaster].carsPerTrain!;
                const car = cars[t * per + (per >> 1)];
                camera.look(car, { col: car.col + 6, row: car.row + 9, up: 14 });
                await frames(20);
                await screenshot("riders leaving every car");
                pictured = true;
            }
            stopped[t] = undefined;
        }
        expect(stopped[t] === undefined || k * kStep - stopped[t]!.since <= kOffWithin, `train ${t}'s riders off within ${kOffWithin} s of it stopping, not ${JSON.stringify(train)}`);
        if (train.station === 0 && train.riders > was.riders)
            ++boarded;
    }
    last = now;
}
log("held trains emptied", heldOff, "at the end", endOff, "boarded", boarded);
expect(endOff >= 3, "trains at the station's end emptied, three times");
expect(boarded >= 1, "guests boarding a train at the station's end");
