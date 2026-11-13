// SPDX-License-Identifier: GPL-3.0-or-later
//
// Arriving guests appear at the park's spawn point, out on its road, and walk in to the gate, where they come
// through. On the blank landscape and on a scenario whose road runs a long way in from the map's edge. Keeps a
// close-up screenshot of a guest just appeared at the spawn point, one on the road, and one at the gate.
//
//   ./nob script scripts/tests/guest_spawn.ts -- --no-audio

export {};

type Tile = { col: number; row: number };
const near = (p: { col?: number; row?: number }, t: Tile, tiles: number) =>
    p.col !== undefined && Math.hypot(p.col - (t.col + 0.5), p.row! - (t.row + 0.5)) < tiles;

/** Looks close at guest `who` from the side and keeps a screenshot named `name`. */
async function look(name: string, who: { col?: number; row?: number }): Promise<void> {
    camera.look({ col: who.col!, row: who.row!, up: 0.8 }, { col: who.col! + 2.5, row: who.row! + 2.5, up: 2.5 });
    await frames(4);
    await screenshot(name);
}

for (const [file, name] of [
    ["Campaigns/Base/BlankLandscape.dat", "blank"],
    ["Campaigns/Base/BoxOffice.dat", "box_office"],
]) {
    await park.open(file);
    await until(() => park.loaded(), 1200, "the park");
    const spawns = park.spawns();
    const gates = park.gates();
    log(name, "spawn points", JSON.stringify(spawns), "gate tiles", JSON.stringify(gates));
    expect(spawns.length > 0, `${name}: a spawn point on the paths`);
    expect(gates.length > 0, `${name}: an entrance on the paths`);
    const spawn = spawns[0];
    camera.look({ col: spawn.col + 0.5, row: spawn.row + 0.5 }, { col: spawn.col + 4.5, row: spawn.row + 4.5, up: 4 });

    // Someone just appeared at a spawn point, not through the gate yet.
    const fresh = () =>
        people.list().findIndex((p) => !p.staff && p.arriving === true && spawns.some((t) => near(p, t, 1.5)));
    await park.advanceUntil(() => fresh() >= 0, 1800, `${name}: a guest appearing at a spawn point`);
    const i = fresh();
    const name_of = people.list()[i].name;
    log(name, "appeared", JSON.stringify(people.list()[i]));
    await look(`${name}_appear`, people.list()[i]);

    // On the road in, then come through the gate, by its tiles.
    await park.advance(3);
    const walking = people.list()[i];
    expect(walking.name === name_of, `${name}: the same guest still listed`);
    log(name, "walking in", JSON.stringify(walking));
    expect(walking.arriving === true, `${name}: still on the way in`);
    await look(`${name}_walk`, walking);
    await park.advanceUntil(() => people.list()[i].arriving !== true, 600, `${name}: the guest through the gate`);
    const inside = people.list()[i];
    log(name, "came in", JSON.stringify(inside));
    expect(
        gates.some((g) => near(inside, g, 2)),
        `${name}: the guest come in at the gate, not at ${inside.col}, ${inside.row}`,
    );
    await look(`${name}_gate`, inside);
}
quit(0);
