// SPDX-License-Identifier: GPL-3.0-or-later
//
// Animal care: a run-down zoo with sick animals and no keepers. Left alone a while, the park is warned that its
// neglected animals will be confiscated. Keepers hired from the staff window feed them, so they mend and no more
// warnings come.
// Screenshots: the warning in the message bar, the animals' window with its habitat bar, and the zoo after the
// keepers have been at work.
//
//   ./nob script scripts/tests/animal_care.ts -- --no-audio

export {};

const kWarning = "WildAnimalMessage_AboutToBeConfiscated";
const kTaken = "WildAnimalMessage_Confiscated";
const told = (key: string) => park.messages().filter((m) => m.key === key);
const keepers = () => animals.houses().reduce((n, h) => n + h.keepers.length, 0);

await park.open("Campaigns/Base/Wild/RunDownZoo.dat");
await park.ready();
await until(() => ui.find("tools/Land/Land") !== undefined, 300, "the tool row");
const start = animals.list();
log("animals", start.map((a) => `${a.species} ${a.health.toFixed(2)}`).join(", "));
expect(start.length > 0, "the zoo's animals");
expect(animals.houses().length > 0, "the zoo's animal houses");
expect(keepers() === 0, "no keepers at the start");

// Left alone, the sickest animals bring a warning within a game month or so.
await park.advanceUntil(() => told(kWarning).length > 0, 900, "a warning that animals will be confiscated");
log(
    "warned of",
    told(kWarning)
        .map((m) => m.name)
        .join(", "),
);
// Which animals have a house of their kind in their enclosure: keepers can feed only those.
const housed = (a: { id: number; enclosure: number }) => {
    const eats = animals.care(a.id)?.house.toLowerCase() ?? "";
    return animals
        .houses()
        .some((h) => h.enclosure === a.enclosure && (eats === "" || h.kind === "" || h.kind.toLowerCase() === eats));
};
log(
    "houses",
    animals
        .houses()
        .map((h) => `${h.kind} in ${h.enclosure}`)
        .join(", "),
);
log(
    "animals' houses",
    start
        .map(
            (a) =>
                `${a.species} in ${a.enclosure} eats at ${animals.care(a.id)?.house ?? "?"} (space ${animals.care(a.id)?.space}, habitat ${animals.care(a.id)?.habitat.toFixed(2)})`,
        )
        .join("; "),
);
const sick = start.filter((a) => a.health < 0.15 && housed(a));
const middle = (sick.length > 0 ? sick : start)[0];
camera.look(
    { col: middle.at.col, row: middle.at.row, up: 1 },
    { col: middle.at.col + 8, row: middle.at.row + 8, up: 8 },
);
await frames(10);
await screenshot("animal_care_warning");

// Keepers for every house, hired from the staff window.
await ui.click(`tools/Management/${must(ui.children("tools/Management"), "tools/Management shown")[0]}`);
await until(() => ui.find("tools/Management/Man_Staff") !== undefined, 120, "the management menu");
await ui.click("tools/Management/Man_Staff");
await until(
    () => under("Staff_Title", "Staff_HireAnimalKeeperIcon") !== undefined,
    120,
    "the staff window's hire buttons",
);
const wanted = animals.houses().length * 2;
for (let i = 0; i < wanted; ++i) {
    await ui.click(
        must(under("Staff_Title", "Staff_HireAnimalKeeperIcon"), "Staff_HireAnimalKeeperIcon under Staff_Title"),
    );
    await frames(2);
}
await until(() => keepers() >= wanted, 60, "the keepers hired");
await ui.click(under("Staff_Title", "close") ?? under("Staff_Title", "Close") ?? "Staff_Title");
await frames(5);
log("keepers", keepers(), "for", animals.houses().length, "houses");

// A while for the keepers to fill the troughs and the animals to eat and mend, then a game month in which no animal
// with a house of its kind is warned of or taken. One with no such house starves whatever the keepers do.
await park.advance(300);
const seen = park.messages().length;
await park.advance(600);
const end = animals.list();
const kept = end.filter(housed);
log("after", end.map((a) => `${a.species} ${a.health.toFixed(2)}${housed(a) ? "" : " (no house)"}`).join(", "));
const later = park
    .messages()
    .slice(seen)
    .filter((m) => m.key === kWarning || m.key === kTaken);
log("told later", later.map((m) => `${m.key} ${m.name}`).join(", "));
expect(kept.length > 0, "animals with a house of their kind");
expect(!later.some((m) => kept.some((a) => a.name === m.name)), "no warning about an animal its keepers feed");
expect(Math.min(...kept.map((a) => a.health)) >= 0.15, "every animal with a house mended past neglect");

// The habitat of the animal the camera looked at, in its window.
const shown = kept.find((a) => a.id === middle.id) ?? kept[0];
animals.open(shown.id);
await until(() => animals.shown() !== undefined, 60, "the animal's window");
await ui.click(`info/${must(info_tab("animalstatsbuttonicon"), "the stats tab")}`);
await frames(10);
const habitat = must(
    ui.find(must(under("info", "WildAnimalStat_Enclosure_value"), "the habitat bar")),
    "habitat shown",
);
log("habitat", habitat.text);
await screenshot("animal_care_window");
await ui.click(`info/${must(info_tab("animalinfobuttonicon"), "the species tab")}`);
await frames(10);
const space = must(ui.find(must(under("info", "AnimalInfo_MinSpace_label"), "the space line")), "space shown");
log("space", space.text);
await screenshot("animal_care_species");
camera.look({ col: shown.at.col, row: shown.at.row, up: 1 }, { col: shown.at.col + 8, row: shown.at.row + 8, up: 8 });
await frames(10);
await screenshot("animal_care_kept");
