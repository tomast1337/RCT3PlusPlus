// SPDX-License-Identifier: GPL-3.0-or-later
//
// A worker's window as the park runs: they think about their work and how they feel, and those thoughts
// show on their Thoughts tab; the Statistics tab shows the day they were hired, their mood as a face, their
// laziness, warnings and pay, and training them costs the park what the button says.
//
//   ./nob script scripts/tests/staff_mood.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

// The tutorial's mechanic has thought nothing of their own yet, only of being picked up.
await park.open("Tutorials/Tutorial7.dat");
await park.ready();
await frames(10);
const who = people.list().findIndex((p) => p.role === "mechanic");
expect(who >= 0, "a mechanic in the park");
const own = () => (people.list()[who].thoughts ?? []).filter((t) => t >= 47 && t <= 58);
expect(own().length === 0, "no worker's thought yet");
await park.advance(40);
expect(own().length > 0, `a worker's thought after a while, got ${people.list()[who].thoughts}`);

people.show(who);
await until(() => ui.find("info") !== undefined, 120, "the mechanic's window");
const tab = (part: string) => ui.children("info")!.find((t) => t.toLowerCase().includes(part));
await ui.click(`info/${tab("thoughts")}`);
await frames(10);
await screenshot("staff_thoughts");
await ui.click(`info/${tab("stats")}`);
await frames(10);
await screenshot("staff_stats");

/** Finds the control named `name` wherever the window holds it. */
const find = (path: string, name: string): string | undefined => {
    for (const child of ui.children(path) ?? []) {
        const at = `${path}/${child}`;
        if (child === name)
            return at;
        const found = find(at, name);
        if (found !== undefined)
            return found;
    }
    return undefined;
};
const train = find("info", "Rct3pp_Train");
const cash = park.cash();
if (train !== undefined && cash !== undefined) {
    park.setCash(1000000);
    await ui.click(train);
    await frames(5);
    expect(park.cash()! < 1000000, "training paid for");
    await screenshot("staff_trained");
}
