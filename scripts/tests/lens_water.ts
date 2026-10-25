// SPDX-License-Identifier: GPL-3.0-or-later
//
// Water on the camera's lens. In the game's Go With The Flow the view comes up out of a lake and the lens is
// soaked, the water running off it in a few seconds, then it looks out into heavy rain, which spots the lens. In
// Monster Lake it rides the water coaster until a splash lands on the lens. With the Water droplets option off the lens
// stays dry.
//
//   ./nob script scripts/tests/lens_water.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

await park.open("Campaigns/Base/GoWithTheFlow.dat");
await park.ready();
options.set("refraction", 1);
options.set("water_droplets", 1);
await frames(30);
expect(camera.lens().drops === 0, "a dry lens to start");

// A tile well inside a lake, its neighbours wet too.
const size = map.size();
let lake: { col: number; row: number; level: number } | undefined; // level: the water's height
for (let row = 2; row < size.height - 2 && !lake; ++row)
    for (let col = 2; col < size.width - 2 && !lake; ++col) {
        const wet = (c: number, r: number) => map.tile(c, r)?.water;
        const here = wet(col, row);
        if (here !== undefined && [wet(col + 1, row), wet(col - 1, row), wet(col, row + 1), wet(col, row - 1)].every((w) => w !== undefined)) {
            const ground = Math.max(...map.tile(col, row)!.corners);
            if (here - ground > 1.5)
                lake = { col: col + 0.5, row: row + 0.5, level: here };
        }
    }
expect(lake !== undefined, "a lake deep enough to dive in");
log("lake", JSON.stringify(lake));
const { col, row, level } = lake!;
// The view looks at the lake's bed (it keeps what it looks at on the ground), from an eye at `height` in the world.
const eyeAt = (height: number) => {
    let up = 3;
    for (let i = 0; i < 3; ++i) {
        camera.look({ col, row }, { col: col + 3, row, up });
        up += height - camera.view().eye[1];
    }
    camera.look({ col, row }, { col: col + 3, row, up });
};
eyeAt(level - 1.2);
await frames(20);
log("under", JSON.stringify(camera.view().eye));
expect(camera.view().eye[1] < level, "the eye under water");
expect(camera.lens().drops === 0, "no drops under water");
await screenshot("under water");
eyeAt(level + 0.8);
await frames(2);
log("over", JSON.stringify(camera.view().eye));
await until(() => camera.lens().drops > 20, 120, "the lens soaked coming up");
const soaked = camera.lens();
log("soaked", JSON.stringify(soaked));
expect(soaked.drops > 20 && soaked.sheets > 0, "the lens soaked coming up");
await screenshot("out of the lake");
await frames(30);
await screenshot("out of the lake later");
await until(() => camera.lens().drops + camera.lens().sheets < soaked.drops / 2, 1200, "the water running off");
await until(() => camera.lens().drops + camera.lens().sheets === 0, 1200, "a dry lens again");

// Rain on a lens looking out level.
park.setWeather("heavy rain");
eyeAt(level + 1.5);
await until(() => camera.lens().drops >= 5, 1200, "rain on the lens");
await frames(60);
await screenshot("rain");
park.setWeather("sunny");

// Riding Monster Lake's water coaster till it splashes the lens.
await park.open("Campaigns/Base/Soaked/MonsterLake.dat");
await park.ready();
const ride = rides.list().findIndex((r) => /seaserpent/i.test(r.symbol) && (r.trains ?? 0) > 0);
expect(ride >= 0, "the water coaster");
rides.setState(ride, "open");
rides.show(ride);
await frames(30);
await ui.click("info/body/RideCamGUI_Enabled");
await until(() => camera.riding()?.eye !== undefined, 120, "the view from a car");
await until(() => camera.lens().drops >= 20, 6000, "a splash on the lens");
await screenshot("splash");
await frames(20);
await screenshot("splash later");

// Off, the lens stays dry.
options.set("water_droplets", 0);
await frames(2);
expect(camera.lens().drops === 0 && camera.lens().sheets === 0, "a dry lens with the option off");
