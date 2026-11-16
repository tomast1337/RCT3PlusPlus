// SPDX-License-Identifier: GPL-3.0-or-later
//
// Switches the park's day between its opening hours and the windows round the middle of the night and of the
// day. Each mode moves the clock into its window and keeps it there while the park runs, and the night is
// looked at.
//
//   ./nob script scripts/tests/day_mode.ts -- --no-audio

export {};

const hour = (h: number) => h / 24;

await park.open("Campaigns/Base/BlankLandscape.dat");
await frames(30);
expect(park.dayClock().mode === "hours", `a new park keeps its opening hours, not ${park.dayClock().mode}`);

park.setDayMode("night");
await frames(120);
let clock = park.dayClock();
expect(clock.mode === "night", `the park keeps the night, not ${clock.mode}`);
expect(
    clock.time >= hour(22) || clock.time < hour(3),
    `the night's clock between 22:00 and 3:00, not ${clock.time * 24}`,
);
const size = map.size();
camera.look(
    { col: size.width / 2, row: size.height / 2 },
    { col: size.width / 2 - 14, row: size.height / 2 - 14, up: 14 },
);
await frames(60);
await screenshot("night");

park.setDayMode("day");
await frames(120);
clock = park.dayClock();
expect(clock.mode === "day", `the park keeps the day, not ${clock.mode}`);
expect(
    clock.time >= hour(10) && clock.time < hour(16),
    `the day's clock between 10:00 and 16:00, not ${clock.time * 24}`,
);

park.setDayMode("hours");
await frames(10);
expect(park.dayClock().mode === "hours", "back to the opening hours");

let refused = false;
try {
    park.setDayMode("dusk" as "day");
} catch {
    refused = true;
}
expect(refused, "a mode with no such name refused");
