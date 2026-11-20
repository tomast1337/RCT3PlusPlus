// SPDX-License-Identifier: GPL-3.0-or-later
//
// A zoo animal's window: a click on an animal out of its crate opens it on the animal's details, with its species;
// its stats show its health; every tab opens, and a new name typed in the name box is the animal's.
// Screenshots: each tab.
//
//   ./nob script scripts/tests/animal_window.ts -- --no-intro --no-audio --seed 1

export {};

await park.open("Campaigns/Base/Wild/ScrubGardens.dat");
await park.ready();
await frames(30);

// Paused, so the animal stays where it is aimed at.
await ui.click("bar/Top_Pause");
await frames(2);
const out = animals.list().filter((a) => !a.crated && !a.escaped);
log("animals", JSON.stringify(animals.list()));
const first = must(out[0], "an animal out of its crate");

// From above, nothing between, then a click on its middle: its window opens on it.
camera.look(
    { col: first.at.col, row: first.at.row, up: 1 },
    { col: first.at.col + 1.5, row: first.at.row + 1.5, up: 12 },
);
await frames(10);
const now = must(
    animals.list().find((a) => a.id === first.id),
    "the animal still there",
);
const at = must(map.screen(now.at.col, now.at.row, now.height + 0.8), "the animal in sight");
pointer.move(at.x, at.y);
await frames(5);
await pointer.click(at.x, at.y);
await until(() => animals.shown() !== undefined, 120, "the animal's window");
const shown = must(animals.shown(), "the window open");
expect(
    animals.list().some((a) => a.id === shown.id && !a.crated),
    `the window on an animal out of its crate, not ${shown.id}`,
);
const animal = must(
    animals.list().find((a) => a.id === shown.id),
    "the clicked animal listed",
);
log("clicked", JSON.stringify(animal));

// Details: its species and what it is doing.
await ui.click(`info/${must(info_tab("guest_options"), "the details tab")}`);
await frames(10);
expect(animals.shown()?.tab === "WildAnimalGUI_NameWindow", `the details tab open, not ${animals.shown()?.tab}`);
const species = must(ui.find(must(under("info", "WildAnimal_Species_value"), "the species line")), "the species shown");
expect((species.text ?? "") !== "", "the animal's species named");
const doing = must(ui.find(must(under("info", "AnimalDoing_label"), "the status line")), "the status line shown");
expect((doing.text ?? "") !== "", "what the animal is doing");
expect(under("info", "CenterInViewIcon") !== undefined, "the button that turns the view to it");
log("species", species.text, "doing", doing.text);
await screenshot("details");

// Every other tab, each with what it shows.
const tabs: [string, string, string][] = [
    ["animalstatsbuttonicon", "WildAnimalGUI_StatsWindow", "stats"],
    ["guest_thoughts", "WildAnimalGUI_ThoughtsWindow", "thoughts"],
    ["animaladoptionicon", "WildAnimalGUI_AdoptionWindow", "adoptions"],
    ["animalinfobuttonicon", "WildAnimalGUI_InfoWindow", "species"],
];
for (const [part, help, name] of tabs) {
    await ui.click(`info/${must(info_tab(part), `the ${name} tab`)}`);
    await frames(10);
    expect(animals.shown()?.tab === help, `the ${name} tab open, not ${animals.shown()?.tab}`);
    if (name === "stats") {
        const health = must(
            ui.find(must(under("info", "WildAnimalStat_Health_value"), "the health bar")),
            "health shown",
        );
        const listed = must(
            animals.list().find((a) => a.id === shown.id),
            "the animal listed",
        ).health;
        expect(
            Math.abs(parseInt(health.text ?? "", 10) - listed * 100) <= 1,
            `its health ${listed}, not ${health.text}`,
        );
        for (const stat of ["Enclosure", "Hunger", "Thirst", "Social", "Cleanliness"])
            expect(under("info", `WildAnimalStat_${stat}_value`) !== undefined, `the ${stat} bar`);
    }
    if (name === "species") {
        const habitat = must(
            ui.find(must(under("info", "AnimalInfo_HabitatLabel_label"), "the habitat line")),
            "habitat shown",
        );
        expect((habitat.text ?? "").length > "Habitat:".length + 1, `the species' habitat, not "${habitat.text}"`);
        log("habitat", habitat.text);
        expect(under("info", "AnimalInfo_House_label") !== undefined, "the house it needs");
    }
    await screenshot(name);
}

// A new name typed in the details' name box is the animal's.
await ui.click(`info/${must(info_tab("guest_options"), "the details tab")}`);
await frames(5);
await ui.click(must(under("info", "AnimalName"), "the name box"));
keys.press("Ctrl+A");
keys.type("Rosie");
keys.press("Enter");
await frames(3);
const renamed = animals.list().find((a) => a.id === shown.id)?.name;
expect(renamed === "Rosie", `the animal renamed, not "${renamed}"`);

// The locate button turns the park's view to it.
await ui.click(must(under("info", "CenterInViewIcon"), "the locate button"));
await frames(3);

// A script opens another animal's window by its id.
const other = out.find((a) => a.id !== shown.id);
if (other !== undefined) {
    animals.open(other.id);
    await until(() => animals.shown()?.id === other.id, 60, "another animal's window");
}

quit();
