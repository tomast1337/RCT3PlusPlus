// SPDX-License-Identifier: GPL-3.0-or-later
//
// Autograph hunters. A VIP come in at the gate draws fans now and then; a fan too far off walks after them along
// the paths, the VIP stops for those who catch up and signs for them one at a time. Screenshots: fans trailing
// the VIP, and one being signed for.
//
//   ./nob script scripts/tests/vip_fans.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

await park.open("Campaigns/Base/ChequeredFlag.dat");
await park.ready();
await frames(30);
await park.advanceUntil(() => park.guests() >= 60, 1200, "guests in the park");
const goal = park.vipGoals().find((g) => g.level === 0)!;
park.setDate(goal.next);
await until(() => park.vips().length === 1, 120, "the VIP to come in");

const fans = (kind: "chasing" | "autograph") => people.list().filter((p) => p.watching === kind);
const look = () => {
    const v = park.vips()[0];
    camera.look({ col: v.col, row: v.row }, { col: v.col - 3, row: v.row + 4, up: 16 });
};
await park.advanceUntil(() => fans("chasing").length >= 1, 600, "a fan walking after the VIP");
log("chasing", fans("chasing").length, "near", fans("autograph").length);
look();
await frames(10);
await screenshot("fans_trailing");

const signing = () => people.list().some((p) => !p.staff && p.gesture === "handticket" && Math.hypot(p.col! - park.vips()[0].col, p.row! - park.vips()[0].row) < 1);
await park.advanceUntil(signing, 300, "a fan signed for");
look();
await frames(3);
log("signed; chasing", fans("chasing").length, "near", fans("autograph").length);
expect(park.vips().length === 1 && !park.vips()[0].leaving, "the VIP still on their visit");
await screenshot("fan_signed");
quit(0);
