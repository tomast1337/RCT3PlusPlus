// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Debug tab's scenario unlock in a career: started from a new career, whose later scenarios are locked, the
// tool opens them and writes the career, so a second use finds nothing left to open.
//
//   ./nob script scripts/tests/debug_career.ts -- --no-audio
//
// Run with an empty data folder (RCT3PP_DATA): the career it starts is a new one.

export {};

career.start(0);
await park.ready();
const opened = debug.use("unlock_scenarios");
log("scenarios opened", opened);
expect(opened > 0, "the career's locked scenarios opened");
expect(debug.use("unlock_scenarios") === 0, "none left locked in the career's file");
quit(0);
