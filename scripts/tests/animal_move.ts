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
const tileNear = (fits: (t: Tile, col: number, row: number) => boolean, near: { col: number; row: number }) => {
    for (let d = 0; d < 60; ++d)
        for (let dc = -d; dc <= d; ++dc)
            for (let dr = -d; dr <= d; ++dr) {
                if (Math.max(Math.abs(dc), Math.abs(dr)) !== d) continue;
                const col = Math.floor(near.col) + dc;
                const row = Math.floor(near.row) + dr;
                const t = map.tile(col, row);
                if (t !== undefined && t.owned && fits(t, col, row)) return { col, row };
            }
    return undefined;
};

await park.open("Campaigns/Base/Wild/RunDownZoo.dat");
await park.ready();
await frames(30);

const settled = () => animals.list().filter((a) => !a.crated && !a.escaped && a.enclosure >= 0);
log("animals", JSON.stringify(animals.list().map((a) => [a.id, a.species, a.enclosure, a.crated])));
const first: Animal = must(settled()[0], "an animal out in an enclosure");
/** Whether every tile within one of (col, row) is in `enclosure`: well inside it, away from its fence and edge. */
const inside = (enclosure: number, col: number, row: number) => {
    for (let dc = -1; dc <= 1; ++dc)
        for (let dr = -1; dr <= 1; ++dr) if (map.tile(col + dc, row + dr)?.enclosure !== enclosure) return false;
    return true;
};
const edge = must(
    tileNear((t) => t.enclosure >= 0 && t.enclosure !== first.enclosure, first.at),
    "a tile of another enclosure",
);
const other = must(map.tile(edge.col, edge.row), "the tile").enclosure;
const to = must(
    tileNear((_, col, row) => inside(other, col, row), edge),
    "a tile well inside the other enclosure",
);
log("moving", first.name, "from", first.enclosure, "to", other, JSON.stringify(to));
/** The ground's height at the middle of tile (col, row), from its corners. */
const ground = (col: number, row: number) =>
    must(map.tile(col, row), "the tile").corners.reduce((a, b) => a + b, 0) / 4;
/** What the animal's window says it is doing. */
const doing = () => ui.find(must(under("info", "AnimalDoing_label"), "what the animal is doing"))?.text;

// From the window: lifted into its crate, which follows the pointer.
const overview = () =>
    camera.look({ col: to.col + 0.5, row: to.row + 0.5 }, { col: to.col + 6, row: to.row + 6, up: 12 });
overview();
await frames(10);
animals.open(first.id);
await until(() => animals.shown() !== undefined, 120, "the animal's window");
await frames(5);
await ui.click(must(under("info", "MoveAnimal"), "the move button"));
await until(() => byId(first.id).held, 60, "the animal held");
expect(byId(first.id).crated, "the animal in its crate");
/** Puts the pointer over the middle of the target tile, as the view now shows it, and lets the crate follow. */
const point = async () => {
    const at = must(map.screen(to.col + 0.5, to.row + 0.5), "the tile in sight");
    pointer.move(at.x, at.y);
    await frames(10);
    const hung = byId(first.id);
    expect(
        Math.floor(hung.at.col) === to.col && Math.floor(hung.at.row) === to.row,
        `the crate over the tile under the pointer, ${JSON.stringify(hung.at)}`,
    );
    return at;
};
await point();
await until(() => doing() === ui.text("WildAnimalState_Crate"), 60, "the window saying it is in its crate");
const hung = byId(first.id);
log("hanging over", JSON.stringify(hung.at), "height", hung.height, "ground", ground(to.col, to.row));
expect(hung.height > ground(to.col, to.row) + 1, "the crate hanging above the ground");
await screenshot("crate_held");
// A close look at the crate hanging over the enclosure, the pointer kept over the same tile. The view keeps its
// target on the ground, so it aims at the ground beyond the crate, where the line from the eye through the crate's
// middle lands: the crate shows in the middle, clear of the windows on top.
const eye = { back: 2.5, up: 7 };
const middle = hung.height - ground(to.col, to.row) + 0.7;
const beyond = eye.back * (1 - eye.up / (eye.up - middle));
camera.look(
    { col: to.col + 0.5 + beyond, row: to.row + 0.5 + beyond },
    { col: to.col + 0.5 + eye.back, row: to.row + 0.5 + eye.back, up: eye.up },
);
await frames(5);
await point();
const seen = must(map.screen(hung.at.col, hung.at.row, byId(first.id).height + 0.7), "the crate in sight");
log("crate on screen", JSON.stringify(seen));
expect(Math.abs(seen.x - 0.5) < 0.1 && Math.abs(seen.y - 0.5) < 0.1, "the crate in the middle of the close look");
await screenshot("crate_held_close");
overview();
await frames(5);
const over = await point();
await pointer.click(over.x, over.y);
await until(() => !byId(first.id).held, 30, "the crate set down");
expect(byId(first.id).enclosure === other, "the animal in the other enclosure");
await until(() => !byId(first.id).crated, 1200, "the crate to open");
await until(() => doing() !== ui.text("WildAnimalState_Crate"), 60, "the window saying it is out of its crate");
await frames(30);
const moved = byId(first.id);
log(
    "moved",
    JSON.stringify(moved),
    "doing",
    doing(),
    "ground",
    ground(Math.floor(moved.at.col), Math.floor(moved.at.row)),
);
expect(moved.enclosure === other && !moved.escaped, "the animal living in the other enclosure");
expect(moved.health >= first.health - 0.05, `the animal as well as before, ${first.health} to ${moved.health}`);
expect(
    Math.abs(moved.height - ground(Math.floor(moved.at.col), Math.floor(moved.at.row))) < 0.5,
    "the animal standing on the enclosure's ground",
);
camera.look({ col: moved.at.col, row: moved.at.row, up: 1 }, { col: moved.at.col + 3, row: moved.at.row + 3.5, up: 3 });
await frames(10);
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
await until(() => doing() === ui.text("WildAnimalState_Escaped"), 60, "the window saying it escaped");
const loose = byId(first.id).at;
camera.look({ col: loose.col, row: loose.row }, { col: loose.col + 2, row: loose.row + 2.5, up: 14 });
await frames(10);
await screenshot("escaped");

quit();
