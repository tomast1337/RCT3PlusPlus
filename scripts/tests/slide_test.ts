// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pool slides put to test and opened: the car waiting at the top sets off empty, slides down and goes off the open
// end out of sight, with no crash, no wreck and no message; the next waiting car takes its place. Given a park
// (--park), its pool slides; without one, a saved design of each kind of pool slide laid on the blank landscape.
//
//   ./nob script scripts/tests/slide_test.ts -- --no-intro --no-audio --seed 1 [--park PARK]

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

const kSlides = /^(BodySlide|RingSlide|ProSlide|MasterBlaster):/;
const slides = () => rides.list().flatMap((r, i) => (!r.flat && !r.demolished && kSlides.test(r.symbol) ? [i] : []));

await frames(300);
let names: string[] = [];
if (park.loaded()) {
    names = slides().map((i) => rides.list()[i].symbol);
}
else {
    // A saved design of each kind, laid where its ghost fits.
    await park.open("Campaigns/Base/BlankLandscape.dat");
    await until(() => ui.find("tools/Paths/Obj_Paths") !== undefined && build.flatRides() !== undefined, 1200, "tools");
    for (const [kind, design] of [["BodySlide", "Reef Rider"], ["RingSlide", "Undertow"], ["ProSlide", "The Vortex"], ["MasterBlaster", "Tubular"]]) {
        expect(build.pick(`tracks/TrackedRides/${kind}/${kind}`, kind, "designs"), `a ${kind} to build`);
        await until(() => ui.find("design_browser") !== undefined, 600, "the designs window");
        await frames(3);
        const row = build.construction()!.designs.findIndex((d) => d.includes(design));
        expect(row >= 0, `the ${design} design among ${build.construction()!.designs.join(", ")}`);
        const list = ui.find("design_browser/content/list")!;
        const button = ui.find("design_browser/content/build")!;
        const unit = (button.bottom - button.top) / 22;
        await pointer.click(list.x, list.top + (18 + 16 * row) * unit);
        await frames(5);
        await ui.click("design_browser/content/build");
        await frames(10);
        let spot: [number, number] | undefined;
        for (let k = 0; k < 60 && spot === undefined; ++k) {
            const at: [number, number] = [0.05 + 0.06 * (k % 15), 0.3 + 0.1 * Math.floor(k / 15)];
            pointer.move(...at);
            await frames(3);
            const c = build.construction();
            if (c !== undefined && c.ghost && !c.clashes)
                spot = at;
        }
        expect(spot !== undefined, `clear ground for the ${kind}`);
        await pointer.click(...spot!);
        await frames(30);
        build.stop();
        await frames(10);
        const laid = rides.list()[rides.list().length - 1];
        log(kind, design, "pieces", laid.pieces, "track", (laid.track ?? []).join(" "));
        names.push(kind);
    }
    // The last one's construction panel shut, as when the player tests from the ride's window: taking out the ground
    // tool and putting it away shuts it.
    await ui.click("tools/Land/Land");
    await frames(3);
    await ui.click("tools/Land/Land_Brush_Height");
    await until(() => ui.find("land_panel/Land_Vertex") !== undefined, 60, "the land panel");
    await frames(5);
    await ui.click("tools/Land/Land_Brush_Height"); // Put away
    await frames(5);
    expect(build.construction() === undefined, "the construction panel shut");
}
const found = slides();
log("pool slides", found.map((i) => `${rides.list()[i].name} ${rides.list()[i].symbol}`).join(", "));
expect(found.length > 0 && found.length >= names.length, `a pool slide to test, not ${found.length} of ${names.join(" ")}`);

const messages = park.messages().length;
for (const slide of found) {
    const info = () => rides.list()[slide];
    const kind = info().symbol.split(":")[0];
    expect(rides.setState(slide, "closed") == null, `${kind} closed`);
    await frames(10);
    expect(rides.setState(slide, "testing") == null, `${kind} tested`);
    await frames(10);
    const length = info().trackLength ?? 0;
    expect(length > 0 && (info().trainsAt ?? []).length > 0, `${kind} run on its track by the park, not ${JSON.stringify(info())}`);
    // Ending in an open end, one car waits at the top and any others are out of sight past that end. A ring slide
    // may instead go round, carried back up by a lift, as a coaster does.
    const at = info().trainsAt!;
    const open_end = /^(EndStraight|Bowl|Funnel)/.test(info().track!.at(-1) ?? "");
    log(kind, "length", length.toFixed(1), "open end", open_end, "trains", JSON.stringify(at.map((t) => t.front.toFixed(1))));
    expect(!open_end || at.filter((t) => t.front < length - 0.01).length === 1, `one car on ${kind}'s track, the rest past its end, not ${JSON.stringify(at)}`);

    // Under way down the slide: a look at the empty car.
    const sliding = () => (info().trainsAt ?? []).findIndex((t) => t.station === null && t.front > 5 && t.front < length - 5);
    await park.advanceUntil(() => sliding() >= 0 && info().trainsAt![sliding()].front > length / 3, 120, `${kind}'s car on its way down`);
    const down = sliding();
    const car = info().cars![down];
    camera.look(car, { col: car.col + 3, row: car.row + 3, up: car.up! + 4 });
    await frames(10);
    await screenshot(`${kind}_testing`);

    // Off the end it is gone from sight, the next car coming down after it; none of it is a crash.
    if (open_end)
        await park.advanceUntil(() => info().trainsAt![down].front >= length - 0.01 || (info().crashes ?? 0) > 0, 180, `${kind}'s car off the end`);
    else {
        let last = info().trainsAt![down].front;
        await park.advanceUntil(() => {
            const now = info().trainsAt![down].front;
            const round = now < last;
            last = now;
            return round || (info().crashes ?? 0) > 0;
        }, 240, `${kind}'s car round past its station`);
    }
    await park.advance(20);
    expect((info().crashes ?? 0) === 0, `no crash on ${kind}, not ${info().crashes}`);
    expect((info().wrecks ?? []).length === 0, `no wrecks on ${kind}`);
    expect((info().moving ?? 0) > 0, `${kind}'s cars still going round`);

    // Opened, it runs on with no crash either; laid on dry land, away from a pool, it may not open.
    const refused = rides.setState(slide, "open");
    log(kind, "opened", refused ?? "yes");
    if (refused != null)
        continue;
    await park.advance(30);
    expect((info().crashes ?? 0) === 0 && (info().wrecks ?? []).length === 0, `no crash on ${kind} once open`);
    expect(info().open === true, `${kind} still open`);
}
const told = park.messages().slice(messages).map((m) => m.key);
expect(!told.some((k) => /crash/i.test(k)), `no crash message, not ${told.join(" ")}`);
