// SPDX-License-Identifier: GPL-3.0-or-later
//
// Moving zoo animals. The move button in an animal's window lifts it into its crate, which hangs from the pointer;
// a click on another enclosure sets the crate down there, and once it opens the animal lives in that enclosure,
// as well as before. animals.move() does the same from a script: into an enclosure, or onto a path outside them
// all, where the animal escapes once out; off the park's land it is refused.
// Screenshots: the crate held over the other enclosure, and the animal out in it.
//
//   ./nob script scripts/tests/animal_move.ts -- --no-intro --no-audio --seed 1

export {};

type Animal = ReturnType<typeof animals.list>[number];
const byId = (id: number) =>
    must(
        animals.list().find((a) => a.id === id),
        `animal ${id}`,
    );
/** The owned tile nearest `near` that `fits`, within 60 tiles. */
const tileNear = (fits: (t: Tile) => boolean, near: { col: number; row: number }) => {
    for (let d = 0; d < 60; ++d)
        for (let dc = -d; dc <= d; ++dc)
            for (let dr = -d; dr <= d; ++dr) {
                if (Math.max(Math.abs(dc), Math.abs(dr)) !== d) continue;
                const col = Math.floor(near.col) + dc;
                const row = Math.floor(near.row) + dr;
                const t = map.tile(col, row);
                if (t !== undefined && t.owned && fits(t)) return { col, row };
            }
    return undefined;
};

await park.open("Campaigns/Base/Wild/RunDownZoo.dat");
await park.ready();
await frames(30);

const settled = () => animals.list().filter((a) => !a.crated && !a.escaped && a.enclosure >= 0);
log("animals", JSON.stringify(animals.list().map((a) => [a.id, a.species, a.enclosure, a.crated])));
const first: Animal = must(settled()[0], "an animal out in an enclosure");
const to = must(
    tileNear((t) => t.enclosure >= 0 && t.enclosure !== first.enclosure, first.at),
    "a tile of another enclosure",
);
const other = must(map.tile(to.col, to.row), "the tile").enclosure;
log("moving", first.name, "from", first.enclosure, "to", other, JSON.stringify(to));

// From the window: lifted into its crate, which follows the pointer.
camera.look({ col: to.col, row: to.row }, { col: to.col + 6, row: to.row + 6, up: 12 });
await frames(10);
animals.open(first.id);
await until(() => animals.shown() !== undefined, 120, "the animal's window");
await frames(5);
await ui.click(must(under("info", "MoveAnimal"), "the move button"));
await until(() => byId(first.id).held, 60, "the animal held");
expect(byId(first.id).crated, "the animal in its crate");
const over = must(map.screen(to.col + 0.5, to.row + 0.5), "the tile in sight");
pointer.move(over.x, over.y);
await frames(10);
const hung = byId(first.id);
log("hanging over", JSON.stringify(hung.at), "height", hung.height);
expect(
    Math.floor(hung.at.col) === to.col && Math.floor(hung.at.row) === to.row,
    "the crate over the tile under the pointer",
);
await screenshot("crate_held");
// A close look at the crate hanging; the hold follows the pointer, so the view goes back before the click.
camera.look(
    { col: hung.at.col, row: hung.at.row, up: hung.height + 2 },
    { col: hung.at.col + 4, row: hung.at.row + 4, up: hung.height + 5 },
);
await frames(5);
await screenshot("crate_held_close");
camera.look({ col: to.col, row: to.row }, { col: to.col + 6, row: to.row + 6, up: 12 });
pointer.move(over.x, over.y);
await frames(10);
await pointer.click(over.x, over.y);
await until(() => !byId(first.id).held, 30, "the crate set down");
expect(byId(first.id).enclosure === other, "the animal in the other enclosure");
await until(() => !byId(first.id).crated, 1200, "the crate to open");
await frames(30);
const moved = byId(first.id);
log("moved", JSON.stringify(moved));
expect(moved.enclosure === other && !moved.escaped, "the animal living in the other enclosure");
expect(moved.health >= first.health - 0.05, `the animal as well as before, ${first.health} to ${moved.health}`);
await screenshot("moved");

// By script: back into the first enclosure.
const back = must(
    tileNear((t) => t.enclosure === first.enclosure, first.at),
    "a tile of the first enclosure",
);
expect(animals.move(first.id, back), "moved back by script");
expect(byId(first.id).crated && byId(first.id).enclosure === first.enclosure, "crated in the first enclosure");
await until(() => !byId(first.id).crated, 1200, "the crate to open again");
expect(byId(first.id).enclosure === first.enclosure && !byId(first.id).escaped, "home again");

// Off the map: refused, and nothing done.
const size = map.size();
expect(!animals.move(first.id, { col: size.width + 5, row: 0 }), "refused off the map");
expect(!byId(first.id).crated, "left as it was");

// Onto a path, outside every enclosure: it escapes once out.
const path = must(
    map.paths().find((p) => !p.queue && map.tile(p.col, p.row)?.owned && map.tile(p.col, p.row)?.enclosure === -1),
    "a path of the park's own",
);
expect(animals.move(first.id, { col: path.col, row: path.row }), "moved onto a path by script");
await until(() => !byId(first.id).crated, 1200, "its crate to open");
await until(() => byId(first.id).escaped, 600, "the animal escaped outside the enclosures");
await screenshot("escaped");

quit();
