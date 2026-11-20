// SPDX-License-Identifier: GPL-3.0-or-later
//
// Zoo dung: a Wild! scenario saved with dung lying in its enclosures keeps it, apart from the litter on its paths,
// drawn where it lay, for keepers to clear. The scenario has no keepers yet, so the dung stays.
//
//   ./nob script scripts/tests/zoo_dung.ts -- --no-intro --no-audio --seed 1

export {};

await park.open("Campaigns/Base/Wild/RunDownZoo.dat");
await until(() => ui.find("tools/Land/Land") !== undefined, 300, "the tool row");

const dung = animals.dung();
log("dung", dung.length, "litter", park.litter().length);
expect(dung.length === 56, "the scenario's 56 pieces of dung");
expect(park.litter().length === 133, "its 133 pieces of litter on the paths");
expect(
    dung.every((d) => d.enclosure >= 0),
    "every piece in an enclosure",
);

// Close up on the enclosure with the most.
const by = new Map<number, number>();
for (const d of dung) by.set(d.enclosure, (by.get(d.enclosure) ?? 0) + 1);
const most = [...by.entries()].sort((a, b) => b[1] - a[1])[0][0];
const here = dung.filter((d) => d.enclosure === most);
const col = here.reduce((s, d) => s + d.col, 0) / here.length;
const row = here.reduce((s, d) => s + d.row, 0) / here.length;
camera.look({ col, row }, { col: col + 2.5, row: row + 2.5, up: 2.5 });
await frames(10);
await screenshot("dung");
