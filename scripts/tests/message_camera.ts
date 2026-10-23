// SPDX-License-Identifier: GPL-3.0-or-later
//
// The camera before a message about a ride: messages about the first ride of the game's Vanilla Hills are
// posted, the drawer is opened and the newest message's camera clicked. The ride's window opens at the tab the
// message names: a long queue at the operating mode, a breakdown at maintenance; a message naming no tab
// keeps the tab last open.
//
//   ./nob script scripts/tests/message_camera.ts -- --no-audio

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

/** Units across the drawer and down it when fully open, and where the newest message's camera sits in it. */
const drawerWidth = 480;
const drawerOpen = 360;
const camera = { fromLeft: 27, fromBottom: 35 };

await park.open("Campaigns/Base/VanillaHills.dat");
await park.ready();
await frames(60);
expect(rides.list().length > 0, "a ride in the park");

async function clickNewest(): Promise<void> {
    // A double press opens the drawer from closed and closes it from anywhere else, and a new message slides it
    // part way down, so it may take two.
    const isOpen = () => {
        const d = ui.find("messages")!;
        return d.bottom - d.top > ((d.right - d.left) * (drawerOpen - 1)) / drawerWidth / 2;
    };
    for (let tries = 0; tries < 3 && !isOpen(); ++tries) {
        const d = ui.find("messages")!;
        await pointer.click(d.x, d.y);
        await pointer.click(d.x, d.y);
        await frames(30);
    }
    const open = ui.find("messages")!;
    await pointer.click(open.left + ((open.right - open.left) * camera.fromLeft) / drawerWidth, open.bottom - ((open.bottom - open.top) * camera.fromBottom) / drawerOpen);
    await frames(30);
}

for (const [key, tab, want] of [
    ["Thought_PI_Ride_LongQueueTime1", 6, "AttractionDetails_RideOpModeHelp"],
    ["RideBrokenMessage", 8, "AttractionDetails_RideMaintenanceHelp"],
    ["RideFixedMessage", -1, "AttractionDetails_RideMaintenanceHelp"],
] as const) {
    park.postMessage(key, 0, tab);
    await frames(10);
    await clickNewest();
    const shown = rides.shown();
    log(key, JSON.stringify(shown));
    expect(shown?.ride === 0, `the ride's window for ${key}`);
    expect(shown?.tab === want, `${want} for ${key}, not ${shown?.tab}`);
    await screenshot(key);
}
quit(0);
