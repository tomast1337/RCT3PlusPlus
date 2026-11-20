// SPDX-License-Identifier: GPL-3.0-or-later
//
// Buying animals in a Wild! scenario that sells young ones: a young female and an adult male come as picked, each
// named after its species, and a helicopter lowers each crate before it opens. Screenshots: the animal list with
// its young rows, and the helicopter lowering a crate.
//
//   ./nob script scripts/tests/buy_animals.ts -- --no-intro --no-audio --seed 1

export {};

await park.open("Campaigns/Base/Wild/RunDownZoo.dat");
await park.ready();
await frames(10);

// The animal list, with its young rows.
const rides = `tools/Rides/${must(ui.children("tools/Rides"), "tools/Rides shown")[0]}`;
if (!ui.find("tools/Rides/Obj_Rides_Animals")) await ui.click(rides);
await until(() => ui.find("tools/Rides/Obj_Rides_Animals") !== undefined, 120, "the rides menu");
await ui.click("tools/Rides/Obj_Rides_Animals");
await until(() => ui.find("build_list/content/list") !== undefined, 120, "the animal list");
await frames(5);
log("animal list", JSON.stringify(ui.children("build_list/content/list/view/content")));
await screenshot("animal_list");
build.stop();
await ui.click(rides);
await frames(5);

// Somewhere one of the park's own animals lives.
const home = must(
    animals.list().find((a) => a.enclosure >= 0 && !a.crated),
    "an animal in an enclosure",
);
const col = Math.floor(home.at.col);
const row = Math.floor(home.at.row);
await look_down(col + 0.5, row + 0.5);
const had = new Set(animals.list().map((a) => a.id));

/** Buys the animal of `age` of the species living here, at its tile; returns the animal bought. */
async function buy(age: "young female" | "adult male") {
    expect(build.pick("WildAnimals/WildAnimals", home.species, age), `the ${age} ${home.species} in the animal list`);
    await pointer.click(...on_view(col + 0.5, row + 0.5));
    await frames(3);
    build.stop();
    const bought = must(
        animals.list().find((a) => !had.has(a.id)),
        `the ${age} bought`,
    );
    had.add(bought.id);
    log("bought", age, JSON.stringify(bought));
    return bought;
}

const young = await buy("young female");
expect(
    !young.adult && !young.male,
    `a young female, not ${young.adult ? "an adult" : "young"} ${young.male ? "male" : "female"}`,
);
expect(young.flying && young.crated, "the young one's crate lowered by the helicopter");
expect(!young.rescued && young.health === 1, "a healthy animal where the scenario does not rescue them");
const grown = await buy("adult male");
expect(
    grown.adult && grown.male,
    `an adult male, not ${grown.adult ? "an adult" : "young"} ${grown.male ? "male" : "female"}`,
);
for (const a of [young, grown])
    expect(a.name.split(" ").length >= 2 && a.name !== home.species, `a name of its own, not "${a.name}"`);

// From the side and above, past the crate to the ground beyond it, clear of the messages at the top.
camera.look({ col: col + 3, row: row - 30 }, { col: col + 3, row: row + 14, up: 34 });
await frames(5);
await screenshot("helicopter_drop");

const animal = (id: number) =>
    must(
        animals.list().find((a) => a.id === id),
        "the animal bought",
    );
await until(() => !animal(young.id).flying && !animal(grown.id).flying, 3000, "the crates set down");
await until(() => !animal(young.id).crated && !animal(grown.id).crated, 3000, "the crates opened");

quit();
