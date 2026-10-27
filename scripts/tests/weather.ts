// SPDX-License-Identifier: GPL-3.0-or-later
//
// The weather on screen: a sunny park, then heavy rain falling with a greyer sky, then a thunderstorm whose
// lightning flashes. Disable weather holds each kind while it is looked at.
//
//   ./nob script scripts/tests/weather.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

await park.open("Campaigns/Base/VanillaHills.dat");
await park.ready();
options.set("disable_weather", 1);
const start = park.weatherLook();
log("start", park.weather(), JSON.stringify(start));
expect(start.forecast !== "", "a forecast from the park file");

// A view over the park's middle, looking across it.
const size = map.size();
camera.look({ col: size.width / 2, row: size.height / 2 }, { col: size.width / 2 - 12, row: size.height / 2 - 12, up: 10 });

park.setWeather("sunny");
await until(() => park.weatherLook().gloom === 0 && park.weatherLook().rain === 0, 3000, "the sky to clear");
await screenshot("sunny");

park.setWeather("heavy rain");
await frames(2);
expect(park.weatherLook().rain < 1, "the rain to come in, not all at once");
await until(() => park.weatherLook().rain === 1, 3000, "the rain to set in");
const wet = park.weatherLook();
log("heavy rain", JSON.stringify(wet));
expect(wet.gloom > 0.5, "a grey sky in heavy rain");
expect(park.weather() === "heavy rain", "the weather held while disabled");
await screenshot("heavy rain");

park.setWeather("thunderstorm");
await until(() => park.weatherLook().flash > 0.5, 6000, "a lightning stroke");
await screenshot("lightning");
expect(park.weather() === "thunderstorm", "the storm held while disabled");
