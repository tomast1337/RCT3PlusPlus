// SPDX-License-Identifier: GPL-3.0-or-later
//
// A click on the park rating in the top bar opens the park window on its rating breakdown: each part of the
// rating with what it adds or takes off, then the week's score. Give it a busy park:
//
//   ./nob script scripts/tests/rating_breakdown.ts -- --no-audio --park PARK

export {};

const kWindow = "PID_Title";
const kWeekSeconds = 150; // Of park time

await until(() => park.loaded(), 1200, "the park");
await frames(10);
// A whole week run, so one has been rated and the parts are known.
await park.advance(kWeekSeconds + 2);
await ui.click("bar/rating");
await until(() => ui.find(kWindow) !== undefined, 120, "the park window");
await frames(10);
await screenshot("rating_breakdown");
for (const part of ["Rct3pp_RatingHappiness", "Rct3pp_RatingRides", "Rct3pp_RatingGuests", "Rct3pp_RatingScore"])
    expect(under(kWindow, `${part}_label`) !== undefined, `the ${part} line`);
quit(0);
