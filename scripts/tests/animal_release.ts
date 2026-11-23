// SPDX-License-Identifier: GPL-3.0-or-later
//
// Setting zoo animals free in a Wild! scenario whose goal asks for animals of a species set free: a young one is
// refused from its window, the player told why; a grown, healthy one is asked about first and goes on Yes, and
// enough of them meet the goal. Screenshots: the refusal, the question, and the publicity note after a release.
//
//   ./nob script scripts/tests/animal_release.ts -- --no-intro --no-audio --seed 1

export {};

const kDay = 90 / 7;

await park.open("Campaigns/Base/Wild/RaidersOfTheLostCoaster.dat", { sandbox: false });
await park.ready();
await frames(10);
const goal = must(
    park.goals().find((g) => g.kind === "ReleaseAnimalsCondition" && g.challenge === null),
    "a goal on animals set free",
);
const target = must(goal.target, "the number to set free");
log("goal", JSON.stringify(goal));

// Paused, so nothing moves while the boxes are open.
await ui.click("bar/Top_Pause");
await frames(2);
debug.use("add_money", 100_000_000);
debug.use("refresh_animals");
const ofKind = () => animals.list().filter((a) => a.species.toLowerCase() === goal.species.toLowerCase());
const home = must(
    animals.list().find((a) => a.enclosure >= 0 && a.species.toLowerCase() === goal.species.toLowerCase()) ??
        ofKind()[0],
    `a ${goal.species} in an enclosure`,
);
const col = Math.floor(home.at.col);
const row = Math.floor(home.at.row);
await look_down(col + 0.5, row + 0.5);

/** Buys an animal of the goal's species of `age` at the home tile; returns its id. */
async function buy(age: "young female" | "adult male"): Promise<number> {
    const had = new Set(animals.list().map((a) => a.id));
    expect(build.pick("WildAnimals/WildAnimals", home.species, age), `the ${age} ${home.species} in the animal list`);
    await pointer.click(...on_view(col + 0.5, row + 0.5));
    await frames(3);
    build.stop();
    return must(
        animals.list().find((a) => !had.has(a.id)),
        `the ${age} bought`,
    ).id;
}

/** Closes the note box telling the player what happened, after a screenshot of it when `shot` is given. */
async function note(shot?: string): Promise<void> {
    await until(() => ui.find("prize_won") !== undefined, 120, "a note box");
    if (shot) await screenshot(shot);
    await ui.click(`prize_won/${must(ui.children("prize_won"), "prize_won shown").at(-1)!}`);
    await until(() => ui.find("prize_won") === undefined, 120, "the note box to close");
}

// A young one is refused from its window, the player told why.
const young = await buy("young female");
animals.open(young);
await until(() => animals.shown()?.id === young, 120, "the young one's window");
await ui.click(`info/${must(info_tab("guest_options"), "the details tab")}`);
await frames(5);
await ui.click(must(under("info", "ReleaseAnimalButtonIcon"), "the release button"));
await frames(2);
expect(ui.find("release_question") === undefined, "no question for a young animal");
await note("release_refused");
expect(
    animals.list().some((a) => a.id === young),
    "the young one still in the park",
);
expect(!animals.release(young), "the script refused too");
await note();

// A grown one from its window: asked first, No keeps it, Yes sets it free.
const grown = must(
    ofKind().find((a) => a.adult && a.health >= 0.95),
    "a grown, healthy one",
).id;
animals.open(grown);
await until(() => animals.shown()?.id === grown, 120, "its window");
await ui.click(must(under("info", "ReleaseAnimalButtonIcon"), "the release button"));
await until(() => ui.find("release_question") !== undefined, 120, "the question");
await screenshot("release_question");
await ui.click("release_question/content/GUI2MiniDialog_Cancel");
await frames(2);
expect(
    animals.list().some((a) => a.id === grown),
    "No keeping it",
);
await ui.click(must(under("info", "ReleaseAnimalButtonIcon"), "the release button"));
await until(() => ui.find("release_question") !== undefined, 120, "the question again");
await ui.click("release_question/content/GUI2MiniDialog_Confirm");
await frames(2);
expect(!animals.list().some((a) => a.id === grown), "Yes setting it free");
expect(animals.shown() === undefined, "its window closed");
await note("release_publicity");

// The rest by script, buying more grown ones while the park has too few.
let freed = 1;
while (freed < target) {
    const next = ofKind().find((a) => a.adult && a.health >= 0.95);
    const id = next ? next.id : await buy("adult male");
    expect(animals.release(id), `animal ${id} set free`);
    await note();
    ++freed;
}
// The goals are read as each day begins, while the park runs.
await ui.click("bar/Top_Pause");
park.setDate(park.date() + kDay);
await frames(10);
const after = must(
    park.goals().find((g) => g.kind === "ReleaseAnimalsCondition" && g.challenge === null),
    "the goal",
);
log("goal after", JSON.stringify(after));
expect(after.now === target && after.met, `the goal met: ${after.now} of ${target}`);
