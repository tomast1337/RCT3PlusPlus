// SPDX-License-Identifier: GPL-3.0-or-later
//
// The clipboard: a ride's name is pasted into its window's name box with Control+V, cut to the box's length
// when too long, kept with Enter, and copied back out with Control+A and Control+C, and from the window's
// title. Whatever the clipboard held before is put back at the end.
//
//   ./nob script scripts/tests/clipboard.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

const saved = clipboard.get();
try {
    await park.open("Campaigns/Base/VanillaHills.dat");
    await park.ready();
    await frames(30);
    const ride = rides.list().findIndex((r) => r.joined);
    expect(ride >= 0, "a ride");
    rides.show(ride);
    await frames(30);
    const box = "info/body/TT_AttractionDialogIconLabelButton";
    const text = () => ui.find(box)?.text;
    const title = ui.find("info/title")?.text;
    expect(title !== undefined && title !== "" && text() === title, "the ride's name in its name box and its title");
    await ui.click(box);

    // Pasting over everything, accents and characters outside the basic plane too.
    const name = "Montanha-Russa Ação 🎢";
    clipboard.set(name);
    keys.press("Ctrl+A");
    keys.press("Ctrl+V");
    await frames(2);
    expect(text() === name, `"${name}" pasted, not "${text()}"`);
    await screenshot("pasted");

    // Too long: as much as the box takes.
    clipboard.set("Long ".repeat(30));
    keys.press("Ctrl+A");
    keys.press("Ctrl+V");
    await frames(2);
    expect(text() === "Long ".repeat(16), `the paste cut to the box's 80 characters, not "${text()}"`);

    // Back to the first name, kept with Enter.
    clipboard.set(name);
    keys.press("Ctrl+A");
    keys.press("Ctrl+V");
    keys.press("Enter");
    await frames(2);
    expect(ui.find("info/title")?.text === name, `the ride renamed, not "${ui.find("info/title")?.text}"`);

    // And out again.
    clipboard.set("");
    keys.press("Ctrl+A");
    keys.press("Ctrl+C");
    await frames(2);
    expect(clipboard.get() === name, `the name copied, not "${clipboard.get()}"`);

    // The title, which cannot be typed in, is selected whole by a press and copied too.
    clipboard.set("");
    await ui.click("info/title");
    keys.press("Ctrl+C");
    await frames(2);
    expect(clipboard.get() === name, `the title copied, not "${clipboard.get()}"`);
    expect(ui.find("info/title") !== undefined, "the ride's window still open, Control+C not taken as the ride camera key");
    await screenshot("title copied");
} finally {
    clipboard.set(saved);
}
