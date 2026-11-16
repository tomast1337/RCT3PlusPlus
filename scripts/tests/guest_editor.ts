// SPDX-License-Identifier: GPL-3.0-or-later
//
// The developer windows' editor on a busy park: F12 shows it, a guest is picked from its list, their happiness set
// low and held there while the park runs on, then let go. The park's other guests go on as they were. Keeps a
// screenshot of the editor with the guest picked, marked in the park.
//
//   ./nob script scripts/tests/guest_editor.ts -- --no-intro --no-audio --seed 1 --park PARK

export {};

await until(() => park.loaded(), 1200, "the park");
await frames(10);
const guest = people.list().findIndex((p) => !p.staff && p.state !== undefined);
expect(guest >= 0, "a guest the simulation walks");

keys.press("F12");
await until(() => editor.shown(), 60, "the editor among the developer windows");
editor.pick(guest);
await frames(2);
const picked = editor.guest();
expect(picked !== undefined, "the guest picked");
log("picked", picked!.name, JSON.stringify(picked!.levels));
expect(
    picked!.name === people.list()[guest].name,
    `the editor names the guest as their window does, not ${picked!.name}`,
);

expect(editor.set("happiness", 0.15), "happiness set");
editor.hold("happiness", true);
expect(editor.guest()!.held.includes("happiness"), "happiness held");
await park.advance(60);
const held = editor.guest()!.levels.happiness!;
expect(Math.abs(held - 0.15) < 0.001, `happiness held at 0.15 through a minute, not ${held}`);
const at = people.list()[guest].at!;
await look_down(at.col, at.row, 25);
await screenshot("editor");

editor.hold("happiness", false);
expect(editor.guest()!.held.length === 0, "nothing held once let go");
