// SPDX-License-Identifier: GPL-3.0-or-later
//
// VIP visits in two of the game's levels with a VIP goal. Moving the calendar up to the first level's visit tells
// the player a month (unless the level was saved told) and then a week ahead; on the day the VIP comes in at the gate, by name, and their window
// shows their patience and opinion of the park. Moving it past their stay sends them home, and as they leave the
// visit is judged and told: a visit that went badly is tried again later.
//
//   ./nob script scripts/tests/vip.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

const kWeek = 90;
const kMonth = 360;

const keys = () => park.messages().map((m) => m.key);

for (const level of ["ChequeredFlag", "TheMoneyPit"]) {
    await park.open(`Campaigns/Base/${level}.dat`);
    await park.ready();
    await frames(30);
    const goal = park.vipGoals().find((g) => g.level === 0);
    expect(goal !== undefined && !goal.met, `${level}: a VIP goal on its first level`);
    log(level, JSON.stringify(goal));
    const posted = park.messages().length;

    // A level may come with the month's notice given already.
    park.setDate(goal!.next - kMonth + 1);
    if (!goal!.toldMonth)
        await until(() => keys().includes("VIPCondition_MonthNotice"), 120, `${level}: the month's notice`);
    park.setDate(goal!.next - kWeek + 1);
    await until(() => keys().includes("VIPCondition_WeekNotice"), 120, `${level}: the week's notice`);
    expect(park.vips().length === 0, `${level}: no VIP before the day`);

    park.setDate(goal!.next);
    await until(() => park.vips().length === 1, 120, `${level}: the VIP to come in`);
    const vip = park.vips()[0];
    expect(vip.name.length > 0, `${level}: the VIP to have a name`);
    expect(park.messages().at(-1)!.key === "VIP_ArrivedInPark" && park.messages().at(-1)!.name === vip.name, `${level}: the arrival told by name`);
    await until(() => park.vips()[0].shown !== null, 600, `${level}: the VIP to be dressed and shown`);
    const at = park.vips()[0];
    camera.look({ col: at.col, row: at.row });
    people.show(at.shown!);
    await frames(30);
    await until(() => ui.find("info") !== undefined, 120, `${level}: the VIP's window`);
    log(level, "window", JSON.stringify(ui.children("info")));
    const stats = ui.children("info")!.find((t) => t.toLowerCase().includes("stat"));
    if (stats !== undefined) {
        await ui.click(`info/${stats}`);
        await frames(10);
    }
    await screenshot(`${level}_vip`);

    // Their stay over, they head out at the gate and the visit is judged.
    park.setDate(goal!.next + goal!.months * kMonth);
    await until(() => park.vips().length === 0 || park.vips()[0].leaving, 600, `${level}: the VIP to head home`);
    expect(keys().includes("VIP_IsLeavingPark"), `${level}: the leaving told`);
    await until(() => keys().includes("VIPCondition_Success") || keys().includes("VIPCondition_Failure"), 20000, `${level}: the visit judged`);
    const told = keys().slice(posted);
    log(level, JSON.stringify(told));
    expect(told.includes("VIP_LeftPark"), `${level}: the VIP's going told`);
    const after = park.vipGoals().find((g) => g.level === 0)!;
    if (told.includes("VIPCondition_Failure"))
        expect(!after.met && after.next > park.date(), `${level}: another visit after one that went badly`);
    else
        expect(after.met, `${level}: the goal met`);
    await screenshot(`${level}_judged`);
}
quit(0);
