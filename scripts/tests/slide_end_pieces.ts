// SPDX-License-Identifier: GPL-3.0-or-later
//
// The pro slide's end pieces built by hand: a station on the blank landscape, two ways round, and after it each of
// the bowls and funnels, left and right, open and enclosed, picked from the building's list. Each must be offered,
// turn into its twin when the enclosed toggle is turned over, show a ghost that may stand, be built as the piece
// picked, and carry a test car along its whole path with no crash: a funnel's path is about 54 m, a bowl's about
// 25 m. Each is shown with the car in it.
//
//   ./nob script scripts/tests/slide_end_pieces.ts -- --no-intro --no-audio --seed 1

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

const kPanel = "placing/body/construction";
// Each piece, its enclosed twin, and the least length of its path.
const kEnds: [string, string, number][] = [["Funnel", "FunnelCapped", 50], ["FunnelRight", "FunnelRightCapped", 50], ["Bowl", "BowlCapped", 22], ["BowlRight", "BowlRightCapped", 22]];

/** Picks the row of the building's list that holds `open` or `enclosed`, whichever the toggles give, going down it
 * with the keys; whether one is picked. */
async function pickListed(open: string, enclosed: string): Promise<boolean> {
    const list = ui.find(`${kPanel}/list`);
    if (!list)
        return false;
    // Not soon after the last press, which would make it a double press that builds.
    await frames(40);
    await pointer.click(list.x, list.top + 0.005);
    await frames(3);
    keys.press("Home");
    await frames(3);
    const seen: string[] = [];
    for (let i = 0; i < 16; ++i) {
        const piece = build.construction()?.piece;
        seen.push(`${piece}`);
        if (piece === open || piece === enclosed)
            return true;
        keys.press("Down");
        await frames(3);
    }
    log("LISTED", seen.join(" "), JSON.stringify(build.construction()));
    return false;
}

await park.open("Campaigns/Base/BlankLandscape.dat");
await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "tools");
park.setWeather("sunny");
park.setTime(0.5);
const { width, height } = map.size();

const failed: string[] = [];
let spot = 0;
for (const turn of [0, 1]) {
    for (const [open, enclosed, least] of kEnds) {
        for (const want of [open, enclosed]) {
            const name = `${want} turn ${turn}`;
            // A station of its own on open ground, away from the last.
            const col = width / 2 - 40 + 16 * (spot % 6);
            const row = height / 2 - 20 + 16 * Math.floor(spot / 6);
            ++spot;
            camera.look({ col, row }, { col, row: row + 30, up: 40 });
            await frames(5);
            expect(build.pick("tracks/TrackedRides/ProSlide/ProSlide", "ProSlide"), "a pro slide to build");
            await until(() => build.construction() !== undefined, 120, "the building panel");
            pointer.move(0.5, 0.5);
            await frames(10);
            for (let i = 0; i < turn; ++i) {
                keys.press(".");
                await frames(2);
            }
            await frames(5);
            expect(/station/i.test(build.construction()!.piece ?? ""), `${name}: a station picked to begin with, not ${build.construction()!.piece}`);
            await pointer.click(0.5, 0.5);
            await frames(20);
            const slide = build.construction()!.ride;
            const info = () => rides.list()[slide];
            expect(info().pieces === 1, `${name}: the station laid`);
            // With the enclosed toggle as wanted first, the list's row gives that one of the twins.
            if (!(await pickListed(open, enclosed))) {
                await screenshot(`not_offered_${want}_turn${turn}`);
                failed.push(`${name}: not offered`);
                build.stop();
                await ui.click("placing/close");
                continue;
            }
            // Else the enclosed toggle turns the piece into its twin.
            if (build.construction()!.piece !== want) {
                await ui.click(`${kPanel}/toggle1`);
                await frames(5);
                if (build.construction()!.piece === want)
                    await pickListed(want, want);
            }
            const c = build.construction()!;
            log("PIECE", name, "picked", c.piece, "ghost", c.ghost, "clashes", c.clashes);
            if (c.piece !== want || !c.ghost || c.clashes) {
                await screenshot(`not_offered_${want}_turn${turn}`);
                failed.push(`${name}: picked ${c.piece}, ghost ${c.ghost}, clashes ${c.clashes}`);
                build.stop();
                await ui.click("placing/close");
                continue;
            }
            // Enter on its row builds it.
            keys.press("Enter");
            await frames(20);
            const built = info().track?.at(-1);
            build.stop();
            await ui.click("placing/close");
            await frames(10);
            if (info().pieces !== 2 || built !== `${want}:tks`) {
                failed.push(`${name}: built as ${built} (${info().pieces} pieces)`);
                continue;
            }
            expect(rides.setState(slide, "testing") == null, `${name}: tested`);
            await until(() => (info().running ?? 0) > 0, 300, `${name}: its car out`);
            const length = info().trackLength ?? 0;
            let farthest = 0;
            let shot = false;
            for (let k = 0; k < 1800 && farthest < length - 1; ++k) {
                await park.advance(1 / 15);
                for (const [i, t] of (info().trainsAt ?? []).entries()) {
                    if (t.station !== null)
                        continue;
                    farthest = Math.max(farthest, t.front);
                    const car = (info().cars ?? [])[i];
                    if (!shot && car && t.front > length - least / 2) {
                        camera.look(car, { col: car.col + 2, row: car.row + 3, up: (car.up ?? 0) + 14 });
                        await frames(4);
                        await screenshot(`slide_end_${want}_turn${turn}`);
                        shot = true;
                    }
                }
            }
            log("RUN", name, "track", length.toFixed(1), "farthest", farthest.toFixed(1), "crashes", info().crashes ?? 0);
            if (length < least + 3 || farthest < length - 1)
                failed.push(`${name}: run ${farthest.toFixed(1)} of ${length.toFixed(1)} m`);
            if ((info().crashes ?? 0) > 0)
                failed.push(`${name}: crashed`);
            rides.setState(slide, "closed");
            await frames(5);
        }
    }
}
expect(failed.length === 0, `every end piece offered, built and run, not: ${failed.join("; ")}`);
