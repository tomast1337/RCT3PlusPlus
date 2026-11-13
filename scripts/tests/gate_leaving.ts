// SPDX-License-Identifier: GPL-3.0-or-later
//
// Guests going home walk out through the gate and are gone: none stays standing on the tiles under the park's
// entrance. Give it a busy park the game saved:
//
//   ./nob script scripts/tests/gate_leaving.ts -- --no-intro --no-audio --seed 1 --park PARK

export {};

await until(() => park.loaded(), 1200, "the park");
await frames(10);
const gates = park.gates();
expect(gates.length > 0, "an entrance on the paths");
const onGate = (col: number, row: number) => gates.some((g) => g.col === Math.floor(col) && g.row === Math.floor(row));
const first = gates[0];
camera.look({ col: first.col, row: first.row }, { col: first.col + 6, row: first.row + 6, up: 25 });
await ui.click("bar/Top_SuperFastForward");

// Each guest on the gate tiles, by their place in people.list, and how many looks in a row they were there.
const kLooks = 80;
const kStuck = 8; // Looks in a row on the gate tiles that count as stuck there
let since = new Map<number, number>();
let left = 0;
let stuck: string[] = [];
for (let look = 0; look < kLooks; ++look) {
    await frames(150);
    const there = people
        .list()
        .map((p, i) => ({ ...p, i }))
        .filter((p) => !p.staff && p.col !== undefined && onGate(p.col, p.row!));
    const next = new Map<number, number>();
    for (const p of there) next.set(p.i, (since.get(p.i) ?? 0) + 1);
    since = next;
    left += there.filter((p) => p.leaving).length;
    stuck = there
        .filter((p) => next.get(p.i)! >= kStuck)
        .map(
            (p) =>
                `${p.i} ${p.name} (${p.state}${p.leaving ? ", leaving" : ""}${p.party !== undefined ? `, party ${p.party}` : ""})`,
        );
    const states = new Map<string, number>();
    for (const p of there)
        states.set(
            `${p.state}${p.leaving ? " leaving" : ""}`,
            (states.get(`${p.state}${p.leaving ? " leaving" : ""}`) ?? 0) + 1,
        );
    log(
        `look ${look} (${park.date()}): longest ${Math.max(0, ...next.values())}, ${there.length} on the gate tiles (${[...states].map(([k, n]) => `${n} ${k}`).join(", ")}), ${stuck.length} there ${kStuck} looks in a row`,
    );
    if (stuck.length > 0) break;
}
await screenshot("gate");
log("guests seen leaving through the gate", left);
expect(stuck.length === 0, `no guest standing on the gate tiles, but ${stuck.join("; ")}`);
quit(0);
