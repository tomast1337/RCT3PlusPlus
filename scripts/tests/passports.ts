// SPDX-License-Identifier: GPL-3.0-or-later
//
// Passports, on the game's blank landscape: two passport posts on the road in from the entrance. Their window
// sets the park's gift and how many stamps it takes; guests dropped on the paths get a stamp at each post they
// pass, and with both the park gives them the gift. Pictures of the posts' window and of a guest's passport.
//
//   ./nob script scripts/tests/passports.ts -- --no-audio

export {};

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => build.stalls() !== undefined, 1200, "the build lists");
const gate = map.paths().reduce((a, b) => (b.row > a.row ? b : a));
const roadAt = (row: number) =>
    must(
        map.paths().find((p) => p.col === gate.col && p.row === row),
        `the road at row ${row}`,
    );

/** Places a passport post on the road's tile at `row`, looking at it from the south first. */
async function placePost(row: number): Promise<void> {
    const tile = roadAt(row);
    expect(
        build.pick("Style/Vanilla/Style", "PStampMachine") ||
            build.pick("Style/Vanilla/PathExtras/PassportStampMachine/PStampMachine", "PStampMachine"),
        "the passport post in the scenery lists",
    );
    camera.look({ col: tile.col + 0.5, row: tile.row + 0.5 }, { col: tile.col + 0.5, row: tile.row + 10.5, up: 25 });
    await frames(3);
    const spot = must(map.screen(tile.col, tile.row), `the road at row ${row} in sight`);
    pointer.move(spot.x, spot.y);
    await frames(3);
    expect(build.placing()?.problem === "none", `the post placeable, not ${JSON.stringify(build.placing())}`);
    await pointer.click(spot.x, spot.y);
    await frames(3);
    build.stop();
    if (ui.find("info") !== undefined) await ui.click("info/close");
    await frames(1);
}

await placePost(gate.row - 4);
await placePost(gate.row - 8);
const posts = park.pathItems().filter((i) => i.kind === "passport post");
expect(posts.length === 2, `two passport posts known to guests, not ${posts.length}`);

// A new park gives a souvenir for a few stamps; here, the last souvenir it offers, for both posts' stamps.
const rules = park.passport();
log("passport", rules.stampsForGift, rules.gift, "of", rules.gifts.join(", "));
expect(rules.gift !== "" && rules.stampsForGift > 0, "a new park gives a gift");
const gift = rules.gifts[rules.gifts.length - 1];
expect(!park.setPassport(0), "no gift for no stamps");
expect(park.setPassport(2, gift), `the gift set to ${gift}`);
expect(park.passport().stampsForGift === 2 && park.passport().gift === gift, "the gift kept");

// The settings, on a post's window.
const first = roadAt(gate.row - 4);
park.setTime(0.5);
camera.look(
    { col: first.col + 0.5, row: first.row + 0.5, up: 0.5 },
    { col: first.col + 2.5, row: first.row + 3.5, up: 3 },
);
park.showObject(posts[0].object);
await frames(3);
await ui.click("info/Guest_Passport");
await frames(5);
expect(ui.find("info/body/Passport_StampsNeededLabelText") !== undefined, "the stamps needed on the post's window");
await screenshot("passports_settings");
await ui.click("info/close");

park.setWeather("sunny");
park.setTime(0.4);
park.dropParties(60, 3);
const gifted = () => people.list().findIndex((p) => !p.staff && p.passport?.gift === true);
for (let ran = 0; gifted() < 0; ran += 5) {
    expect(ran < 1800, "a guest given the gift within 1800 park seconds");
    park.setWeather("sunny");
    await park.advance(5);
}
const guest = people.list()[gifted()];
const passport = must(!guest.staff ? guest.passport : undefined, "the gifted guest's passport");
log(
    "gifted",
    guest.name,
    passport.stamps.map((s) => `${(s.colour >>> 0).toString(16)} at ${s.x.toFixed(2)},${s.y.toFixed(2)}`).join("; "),
);
expect(passport.stamps.length === 2, "two stamps for the gift");
expect(new Set(passport.stamps.map((s) => s.post)).size === 2, "one stamp from each post");

// The guest's passport.
park.setTime(0.5);
people.show(gifted());
await frames(3);
await ui.click("info/Guest_Passport");
await frames(5);
expect(ui.find("info/body/PassportStamp_1") !== undefined, "both stamps on the guest's passport");
await screenshot("passports_guest");
quit(0);
