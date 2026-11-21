// SPDX-License-Identifier: GPL-3.0-or-later
//
// A scenario's challenge: after its wait it is offered in a box that tells what it asks and what it gives, with
// yes, put off and no. Taken on, the challenges window shows it running with its time limit and days left.
//
//   ./nob script scripts/tests/challenges.ts -- --no-audio

export {};

const kDay = 90 / 7;

await park.open("Campaigns/Base/Soaked/MonsterLake.dat");
await park.ready();
await until(() => ui.find("tools/Management") !== undefined, 1200, "the tool row");
await frames(30);
expect(ui.find("challenge_offered") === undefined, "no challenge offered as it opens");

// Its first challenge waits some three weeks: move the calendar on a day at a time until it is offered.
for (let day = 0; day < 40 && ui.find("challenge_offered") === undefined; ++day) {
    park.setDate(park.date() + kDay);
    await frames(10);
}
await until(() => ui.find("challenge_offered") !== undefined, 600, "the challenge offered");
await frames(30);
const box = must(ui.find("challenge_offered"), "challenge_offered shown");
log("offer", JSON.stringify(box.text));
expect(!(box.text ?? "").includes("%AMOUNT1%"), "its figure written in");
const answers = must(ui.children("challenge_offered/content"), "the answers").filter(
    (n) => n !== "text" && n !== "hover",
);
log("answers", JSON.stringify(answers));
await screenshot("challenge_offer");

expect(answers.length === 3, "yes, put off and no");
await ui.click(`challenge_offered/content/${answers[0]}`); // Yes
await until(() => ui.find("challenge_offered") === undefined, 120, "the box closed");
await frames(30);
// The hint for a challenge taken on, if it has one, comes in a box of its own.
if (ui.find("prize_won") !== undefined) {
    await screenshot("challenge_hint");
    await ui.click(`prize_won/${must(ui.children("prize_won"), "prize_won shown").at(-1)!}`);
    await frames(30);
}

// Two days on, its time left is shorter and its goal has been looked at.
park.setDate(park.date() + 2 * kDay);
await frames(10);

if (ui.find("tools/Management/Man_Challenges") === undefined)
    await ui.click(`tools/Management/${must(ui.children("tools/Management"), "tools/Management shown")[0]}`);
await until(() => ui.find("tools/Management/Man_Challenges") !== undefined, 120, "the management menu");
await ui.click("tools/Management/Man_Challenges");
await until(() => under("", "ChallengeWindow_Title") !== undefined, 120, "the challenges window");
await frames(30);
await screenshot("challenge_running");
quit(0);
