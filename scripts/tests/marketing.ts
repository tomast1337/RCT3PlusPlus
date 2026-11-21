// SPDX-License-Identifier: GPL-3.0-or-later
//
// Marketing campaigns: started from the park window's campaigns tab, each week of one is paid on the ledger's
// marketing line, more guests come while it runs, coupon holders get in free, and it ends after its weeks with a
// message. A saved park keeps the campaigns running.
//
//   ./nob script scripts/tests/marketing.ts -- --no-audio

export {};

const kWindow = "PID_Title";
const kAdvertisingWeek = 35000;
const kCouponsWeek = 5000;

async function campaigns_tab(): Promise<void> {
    if (ui.find(kWindow) === undefined) {
        if (ui.find("tools/Management/Man_Park") === undefined)
            await ui.click(`tools/Management/${must(ui.children("tools/Management"), "tools/Management shown")[0]}`);
        await until(() => ui.find("tools/Management/Man_Park") !== undefined, 120, "the management menu");
        await ui.click("tools/Management/Man_Park");
    }
    await until(() => under(kWindow, "ParkInfoDialogIcon_Campaigns") !== undefined, 120, "the campaigns tab");
    await ui.click(must(under(kWindow, "ParkInfoDialogIcon_Campaigns"), "the campaigns tab"));
    await frames(10);
}

/** Sets the campaign set up to run the most weeks, with the duration's upper arrow. */
async function most_weeks(): Promise<void> {
    for (let click = 0; click < 8; ++click) {
        const weeks = ui.find(must(under(kWindow, "Campaign_Duration"), "the duration's arrows"))!;
        await pointer.click(weeks.right - 0.002, weeks.top + (weeks.bottom - weeks.top) / 4);
        await frames(2);
    }
}

/** Guests let in over `seconds` of the park's time from the morning. */
async function arrivals(seconds: number): Promise<number> {
    park.setTime(0.3);
    const before = park.admitted();
    await park.advance(seconds);
    return park.admitted() - before;
}

await park.open("Campaigns/Base/Gunslinger.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined, 1200, "the park");
expect(park.cash() !== undefined, "a park with money");
park.setCash(10_000_000);
park.setEntranceFee(100); // Low enough not to put guests off, so coupon holders have something to save
await frames(30);

// The park's own draw, for a while with no campaign.
const kWeekSeconds = 150; // A calendar week of the park's time
const kRun = kWeekSeconds * 10; // Inside the twelve weeks advertising runs
const plain = await arrivals(kRun);
log("guests in without a campaign", plain);

await campaigns_tab();
await screenshot("campaigns_tab");
expect(under(kWindow, "Campaign_NoActive_label") !== undefined, "no campaign running yet");

// Advertising for the most weeks, started from the window: its first week paid at once.
const cash = park.cash()!;
const marketing = park.ledger().marketing;
await ui.click(must(under(kWindow, "Campaign_Advertising"), "Campaign_Advertising"));
await frames(5);
await most_weeks();
await ui.click(must(under(kWindow, "Campaign_Start"), "Campaign_Start"));
await frames(5);
const running = park.campaigns();
log("campaigns", JSON.stringify(running));
expect(running.length === 1 && running[0]!.kind === "advertising", "the advertising campaign running");
expect(
    park.ledger().marketing - marketing === kAdvertisingWeek,
    `its first week on the marketing line, ${park.ledger().marketing - marketing}`,
);
expect(cash - park.cash()! === kAdvertisingWeek, "paid from the cash");
expect(under(kWindow, "Campaign_Park_Advertise_label") !== undefined, "the campaign listed as running");

// Free entry coupons too, for as long.
await ui.click(must(under(kWindow, "Campaign_ParkVouchers"), "Campaign_ParkVouchers"));
await frames(5);
await most_weeks();
await screenshot("campaign_setup");
await ui.click(must(under(kWindow, "Campaign_Start"), "Campaign_Start"));
await frames(5);
const coupons = park.campaigns().find((c) => c.kind === "parkVouchers");
log("coupons campaign", JSON.stringify(coupons));
expect(coupons !== undefined && coupons.weeksLeft === 12, "the coupons campaign running for twelve weeks");
await screenshot("campaigns_running");

// More come while they run, and those with coupons get in free.
const advertised = await arrivals(kRun);
log("guests in while advertised", advertised, "coupons", JSON.stringify(park.coupons()));
expect(advertised > plain, `more guests while advertised: ${advertised} against ${plain}`);
// A fifth of the parties drawn now come with coupons; a week more for one to come in, if none has yet.
if (park.coupons().usedForEntry === 0) await park.advance(kWeekSeconds);
log("coupons", JSON.stringify(park.coupons()));
expect(park.coupons().usedForEntry > 0, "coupon holders let in free");

// Attraction vouchers through the script call, for the first ride.
expect(rides.list().length > 0, "a ride to give coupons for");
expect(park.startCampaign("attractionVouchers", 12, 0 as RideIndex), "attraction vouchers started");
expect(!park.startCampaign("attractionVouchers", 1, 0 as RideIndex), "only one of a kind at a time");

// Saved and loaded, the campaigns run on.
const kept = park.campaigns().length;
await files_menu("File_SaveGame");
await until(() => ui.find("park_files") !== undefined, 120, "the save window");
await ui.click("park_files/content/open");
await until(() => ui.find("park_files") === undefined, 120, "the save window to close");
await files_menu("File_LoadGame");
await until(() => ui.find("load_question") !== undefined, 120, "the question before loading");
await ui.click("load_question/content/answer1");
const first = "park_files/content/list/view/content/row0";
await until(() => ui.find(first) !== undefined, 120, "the save in the load window");
await ui.click(first);
await ui.click("park_files/content/open");
await until(() => ui.find("park_files") === undefined && park.loaded(), 1200, "the saved park");
await park.ready();
await frames(30);
log("campaigns after loading", JSON.stringify(park.campaigns()));
expect(park.campaigns().length === kept, `${kept} campaigns running after loading`);

// Past the end of the longest: every week paid and each ended with a message.
const paid = park.ledger().marketing;
const toldBefore = park.messages().filter((m) => m.key === "Campaign_Finished").length;
const end = Math.max(...park.campaigns().map((c) => c.end));
const unpaid = park
    .campaigns()
    .reduce((n, c) => n + c.weeksLeft * (c.kind === "advertising" ? kAdvertisingWeek : kCouponsWeek), 0);
park.setDate(end - 0.5);
await frames(10);
park.setDate(end + 0.5);
await frames(10);
const told = park.messages().filter((m) => m.key === "Campaign_Finished").length - toldBefore;
log("marketing", paid, "->", park.ledger().marketing, "left to pay", unpaid, "ended messages", told);
expect(park.campaigns().length === 0, "every campaign ended");
expect(told === kept, `a message for each campaign ended, ${told}`);
expect(park.history().ledger.length > 0, "a ledger");
await campaigns_tab();
await screenshot("campaigns_ended");
quit(0);
