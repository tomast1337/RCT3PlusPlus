// SPDX-License-Identifier: GPL-3.0-or-later
//
// On-ride photos: a coaster's photo section photographs the riders of each train going by, and the photo is saved
// to the photos folder under the ride's name, as the player is told. With the coaster photos option off, trains
// pass the camera without a photo. Give it a park with guests riding a coaster that has a photo section:
//
//   ./nob script scripts/tests/ride_photos.ts -- --no-audio --park PARK

export {};

await park.ready();
await frames(30);
options.set("all_photos", 1);
options.set("coaster_camera_photos", 1);

const shooting = rides
    .list()
    .filter((r) => r.kind === "tracked" && r.joined && r.open && r.track.some((p) => /photosection/i.test(p)));
expect(shooting.length > 0, "an open coaster with a photo section");
log("rides with a photo section", shooting.map((r) => r.name).join(", "));

const photos = () => park.messages().filter((m) => m.key === "Rct3pp_PhotoSaved");
/** Runs the park on in short steps, each drawn, as photos are taken where the cars are drawn: until `check`, or for
 *  `seconds` of park time; whether `check` came true. */
async function drive(seconds: number, check: () => boolean): Promise<boolean> {
    for (let t = 0; t < seconds && !check(); t += kStep) await park.advance(kStep);
    return check();
}
const kStep = 0.05; // Park seconds a drawn frame: a car moves a metre or so
const kPhotos = 4;
expect(await drive(600, () => photos().length >= kPhotos), `${kPhotos} photos of riders`);
for (const saved of photos()) {
    log("saved", saved.name);
    expect(
        shooting.some((r) => saved.name.startsWith(`${r.name} photo_`)),
        `the photo named after a ride with a photo section, not ${saved.name}`,
    );
}

// The ride last photographed, by the front car of its first train.
const ride = shooting.find((r) => photos().at(-1)!.name.startsWith(r.name))!;
const car = must(tracked(rides.list()[ride.index], ride.name).cars[0], "the train's front car");
camera.look({ col: car.col, row: car.row, up: car.up }, { col: car.col + 6, row: car.row + 6, up: (car.up ?? 0) + 12 });
await frames(5);
await screenshot("photo_taken");

options.set("coaster_camera_photos", 0);
const off = photos().length;
// Several laps' worth: each train passes the camera more than once.
await drive(150, () => false);
expect(photos().length === off, `no photos with the option off, not ${photos().length - off}`);
options.set("coaster_camera_photos", 1);
