// SPDX-License-Identifier: GPL-3.0-or-later
//
// An entertainer's show. An entertainer hired from the staff window and put down where the guests walk puts on
// a show once they find guests near: guests gather in a ring round them, facing them, and after a good show they
// clap, cheer or laugh. Screenshots: the crowd round the show, and the applause after it.
//
//   ./nob script scripts/tests/entertainer_show.ts -- --no-audio

export {};

/** The path of the widget named `name` somewhere under `path`. */

await park.open("Campaigns/Base/ChequeredFlag.dat");
await park.ready();
await frames(30);
await park.advanceUntil(() => park.guests() >= 40, 1200, "guests in the park");

// The path tile with the most guests walking about it, off the queues.
const paths = map.paths().filter((p) => !p.queue && !p.raised);
const walkers = () => people.list().filter((p) => !p.staff && p.state === "walking" && p.col !== undefined && p.row !== undefined);
const crowd_at = (col: number, row: number) => walkers().filter((p) => Math.abs(p.col! - col - 0.5) < 2 && Math.abs(p.row! - row - 0.5) < 2).length;
const busy = paths.reduce((a, b) => (crowd_at(b.col, b.row) > crowd_at(a.col, a.row) ? b : a));
camera.look({ col: busy.col + 0.5, row: busy.row + 0.5 }, { col: busy.col + 4, row: busy.row + 4, up: 9 });
await frames(5);

await ui.click(`tools/Management/${must(ui.children("tools/Management"), "tools/Management shown")[0]}`);
await until(() => ui.find("tools/Management/Man_Staff") !== undefined, 120, "the management menu");
await ui.click("tools/Management/Man_Staff");
await until(() => under("Staff_Title", "Staff_HireEntertainerIcon") !== undefined, 120, "the staff window's hire buttons");
const count = staff().length;
await ui.click(must(under("Staff_Title", "Staff_HireEntertainerIcon"), "Staff_HireEntertainerIcon under Staff_Title"));
await until(() => staff().length === count + 1, 60, "an entertainer hired");
const star = () => staff()[count];
expect(star().role === "entertainer", `an entertainer, not ${star().role}`);
await until(() => star().state === "held", 30, "the entertainer in hand");
// The window out of the way, then put down on the busy tile.
await ui.click(under("Staff_Title", "close") ?? under("Staff_Title", "Close") ?? "Staff_Title");
await frames(5);
const at = must(map.screen(busy.col + 0.5, busy.row + 0.5), `tile ${busy.col + 0.5}, ${busy.row + 0.5} in sight`);
pointer.move(at.x, at.y);
await frames(5);
await pointer.click(at.x, at.y);
await until(() => star().state !== "held", 60, "the entertainer let go");

// A show, with a crowd round it.
const watchers = () => people.list().filter((p) => p.watching === "show");
const look_at_star = () => {
    const s = star();
    if (s.col !== undefined && s.row !== undefined)
        camera.look({ col: s.col, row: s.row, up: (s.up ?? 0) + 1 }, { col: s.col - 1, row: s.row + 1.2, up: (s.up ?? 0) + 3.5 });
};
await park.advanceUntil(() => star().show !== undefined, 600, "a show");
log("show begins with", star().show);
await park.advanceUntil(() => watchers().length >= 3 || star().show === undefined, 30, "a crowd");
await park.advance(2); // Time to step into the ring
look_at_star();
await frames(10);
log("crowd", watchers().length, "move", star().show);
expect(watchers().length >= 1, "guests watching the show");
await screenshot("show_crowd");

const clapping = () => people.list().filter((p) => !p.staff && (p.gesture === "applause" || p.gesture === "jumpjoy" || p.gesture === "laugh"));
await park.advanceUntil(() => star().show === undefined, 60, "the show over");
look_at_star();
await frames(3);
log("applause", clapping().length, "grumbles", people.list().filter((p) => p.gesture === "shakehead").length);
expect(clapping().length >= 1, "the crowd clapping after the show");
await screenshot("show_applause");
