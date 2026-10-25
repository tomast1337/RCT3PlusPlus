// SPDX-License-Identifier: GPL-3.0-or-later
//
// Animal enclosures. In a park of the Wild! expansion the park keeps which tiles each enclosure takes, and its
// fences stand again round them when it loads. There, a drag with a fence from the enclosure list fences the
// tiles round, paid for. An animal house goes only inside it, with its keeper's hut and a keeper, who walks
// about the enclosure. An animal goes only inside it too, coming in a crate. A drag with the list's delete tool
// takes some of the tiles out again. Saved and loaded, the enclosures, the keeper and the animals are all back.
//
//   RCT3PP_DATA=<an empty folder> ./nob script scripts/tests/enclosures.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

/** Where the middle of tile (col, row) is on the view; throws when it is out of sight. */
function on_view(col: number, row: number): [number, number] {
    const at = map.screen(col, row);
    if (!at)
        throw new Error(`tile ${col}, ${row} is not in sight`);
    return [at.x, at.y];
}

await park.open("Campaigns/Base/Wild/ScrubGardens.dat");
await until(() => ui.find("tools/Land/Land") !== undefined, 600, "the tool row");
const panels = map.objects().filter((o) => o.overlay.startsWith("Enclosures/Fences/"));
log(`${panels.length} enclosure fence panels`);
expect(panels.length > 100, `the enclosures fenced, not ${panels.length} panels`);
const mid = panels[Math.floor(panels.length / 2)];
camera.look({ col: mid.col + 0.5, row: mid.row + 0.5 }, { col: mid.col + 12, row: mid.row + 14, up: 18 });
await frames(30);
await screenshot("scrub_gardens_fences");

const fenced = () => map.objects().filter((o) => o.overlay.startsWith("Enclosures/Fences/")).length;

// A five by four rectangle of the park's own land, a tile clear of the enclosures and of paths.
const { width, height } = map.size();
const paths = map.paths();
const clear = (c: number, r: number) => {
    for (let row = r - 1; row <= r + 4; ++row)
        for (let col = c - 1; col <= c + 5; ++col)
        {
            const tile = map.tile(col, row);
            if (!tile?.owned || tile.enclosure >= 0 || paths.some((p) => p.col === col && p.row === row))
                return false;
        }
    return true;
};
let site: { col: number; row: number } | undefined;
for (let r = 1; r < height - 5 && !site; r += 2)
    for (let c = 1; c < width - 6 && !site; c += 2)
        if (clear(c, r))
            site = { col: c, row: r };
expect(site !== undefined, "room for an enclosure");
const c0 = site!.col;
const r0 = site!.row;
camera.look({ col: c0 + 2.5, row: r0 + 2 }, { col: c0 + 2.5, row: r0 + 14, up: 20 });
await frames(10);
const cash = park.cash()!;
expect(build.pick("Enclosures/Fences/Wood/Wood_Stub", "Wood"), "the wooden fence in the enclosure list");
await pointer.drag(on_view(c0, r0), on_view(c0 + 4, r0 + 3), 8);
await frames(3);
expect(fenced() === panels.length + 18, `a five by four enclosure fenced, not ${fenced() - panels.length} panels`);
log("fence paid", cash - park.cash()!);
expect(park.cash()! < cash, "the fence paid for");
await screenshot("enclosure_built");

// An animal house goes only inside an enclosure; its keeper's hut goes up beside it and a keeper is hired.
const keepers = () => people.list().filter((p) => p.staff).length;
const staff = keepers();
expect(build.pick("Enclosures/Shelters/Shelters", "CattleShelter"), "the cattle shelter in the enclosure list");
pointer.move(...on_view(c0, r0)); // Over the fence
await frames(3);
expect(build.placing()?.problem === "off enclosure", `the shelter refused outside the enclosure, not ${build.placing()?.problem}`);
pointer.move(...on_view(c0 + 1, r0 + 1));
await frames(3);
expect(build.placing()?.problem === "none", `the shelter fits inside, not ${build.placing()?.problem}`);
await pointer.click(...on_view(c0 + 1, r0 + 1));
await frames(3);
build.stop();
await frames(10);
const huts = map.objects().filter((o) => o.symbol === "WardensShed" && o.col >= c0 && o.col < c0 + 5 && o.row >= r0 && o.row < r0 + 4);
expect(map.objects().some((o) => o.symbol === "CattleShelter" && o.col >= c0 && o.row >= r0), "the shelter placed");
expect(huts.length === 1, `one keeper's hut in the enclosure, not ${huts.length}`);
await until(() => keepers() === staff + 1, 300, "a keeper hired");
const keeper = () => people.list().filter((p) => p.staff).at(-1)!;
log("keeper at", JSON.stringify(keeper().at));
const hut = huts[0];
// Out in the open part of the enclosure, beside the house.
await until(() => keeper().at!.col >= c0 + 3.3 || (keeper().at!.row >= r0 + 3.3 && Math.abs(keeper().at!.col - hut.col - 0.5) > 0.8), 6000, "the keeper out in the open");
const k = keeper().at!;
log("keeper at", JSON.stringify(k));
expect(map.tile(Math.floor(k.col), Math.floor(k.row))?.enclosure === map.tile(c0, r0)!.enclosure, "the keeper in their enclosure");
camera.look({ col: k.col, row: k.row }, { col: k.col + 3, row: k.row + 5, up: 4 });
await frames(5);
await screenshot("enclosure_shelter_keeper");
camera.look({ col: c0 + 2.5, row: r0 + 2 }, { col: c0 + 2.5, row: r0 + 14, up: 20 });

// An animal from the animal list comes down in an enclosure, paid for, in a crate that opens after a while.
expect(build.pick("WildAnimals/WildAnimals", "zebra"), "the zebra in the animal list");
const before = park.cash()!;
await pointer.click(...on_view(c0 + 3, r0 + 2));
await frames(3);
build.stop();
const first = animals.list()[0]?.id;
const zebra = () => animals.list().find((a) => a.id === first)!;
expect(animals.list().length === 1 && zebra().crated, "the zebra in its crate");
expect(Math.floor(zebra().at.col) === c0 + 3 && Math.floor(zebra().at.row) === r0 + 2, `the crate where it was put, not ${JSON.stringify(zebra().at)}`);
expect(park.cash()! < before, "the zebra paid for");
camera.look({ col: c0 + 3.5, row: r0 + 2.5 }, { col: c0 + 5, row: r0 + 7, up: 4 });
await frames(10);
await screenshot("enclosure_crate");
await until(() => !zebra().crated, 3000, "the crate opened");
expect(!zebra().escaped, "the zebra at home in its enclosure");
camera.look({ col: zebra().at.col, row: zebra().at.row, up: 1 }, { col: zebra().at.col + 2.5, row: zebra().at.row + 2.5, up: 2.5 });
await frames(10);
await screenshot("zebra_out");

// One put down outside every enclosure gets out as its crate opens; the park says so, and the keeper goes after
// it, crates it and takes it home.
expect(build.pick("WildAnimals/WildAnimals", "zebra"), "the zebra in the animal list");
await pointer.click(...on_view(c0 + 2, r0 - 2));
await frames(3);
build.stop();
expect(animals.list().length === 2, "a second zebra put down outside the enclosure");
const second = animals.list().find((a) => a.id !== first)!.id;
const loose = () => animals.list().find((a) => a.id === second)!;
await until(() => loose().escaped, 3000, "the zebra out");
await until(() => park.messages().some((m) => m.key === "Thought_PI_AnimalEscaped1"), 60, "the park saying a zebra got out");
camera.look({ col: loose().at.col, row: loose().at.row }, { col: loose().at.col + 2, row: loose().at.row + 6, up: 4 });
await frames(10);
await screenshot("animal_escaped");
await until(() => !loose().escaped && loose().enclosure === map.tile(c0, r0)!.enclosure, 9000, "the keeper bringing the zebra home");
camera.look({ col: c0 + 2.5, row: r0 + 2 }, { col: c0 + 2.5, row: r0 + 14, up: 20 });
await frames(10);
await screenshot("animal_recaptured");
camera.look({ col: c0 + 2.5, row: r0 + 2 }, { col: c0 + 2.5, row: r0 + 14, up: 20 });
await frames(5);
expect(build.pick("", "EnclosureDelete"), "the enclosure delete tool");
await pointer.drag(on_view(c0 + 4, r0), on_view(c0 + 4, r0 + 3), 6);
await frames(3);
expect(fenced() === panels.length + 16, `four by four left, not ${fenced() - panels.length} panels`);
build.stop();
await frames(60);
await screenshot("enclosure_cut");

// Saved and loaded again: the enclosure, its keeper and both zebras come back.
const kept = { fences: fenced(), keepers: keepers(), animals: animals.list().length, enclosure: map.tile(c0 + 1, r0 + 1)!.enclosure };
await ui.click(`tools/Files/${ui.children("tools/Files")![0]}`);
await ui.click("tools/Files/File_SaveGame");
await until(() => ui.find("park_files") !== undefined, 120, "the save window");
await ui.click("park_files/content/open");
await until(() => ui.find("park_files") === undefined, 120, "the save window to close");
if (!ui.find("tools/Files/File_LoadGame"))
    await ui.click(`tools/Files/${ui.children("tools/Files")![0]}`);
await until(() => ui.find("tools/Files/File_LoadGame") !== undefined, 120, "the files menu");
await ui.click("tools/Files/File_LoadGame");
const saved_row = "park_files/content/list/view/content/row0";
await until(() => ui.find(saved_row) !== undefined, 120, "the save in the load window");
await ui.click(saved_row);
await ui.click("park_files/content/open");
await until(() => animals.list().length === kept.animals, 1200, `${kept.animals} animals back after loading`);
await frames(30);
expect(fenced() === kept.fences, `${kept.fences} fence panels after loading, not ${fenced()}`);
expect(map.tile(c0 + 1, r0 + 1)!.enclosure === kept.enclosure, "the enclosure where it was");
await until(() => keepers() === kept.keepers, 600, "the keeper back");
camera.look({ col: c0 + 2.5, row: r0 + 2 }, { col: c0 + 2.5, row: r0 + 14, up: 20 });
await frames(10);
await screenshot("enclosure_loaded");
