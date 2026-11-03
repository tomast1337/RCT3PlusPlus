// SPDX-License-Identifier: GPL-3.0-or-later
//
// Swimmers whole in the water, and what guests wear: their swimwear inside a pool complex, their clothes
// everywhere else. A guest picked up off a path and let go over a pool falls in and swims there, a puff of
// sparkles hiding the change into their swimwear; picked out again and let go on a path, another puff hides
// the change back. Give it a park the game saved with guests swimming:
//
//   ./nob script scripts/tests/pool_outfits.ts -- --no-intro --no-audio --seed 1 --park PARK

export {};

function expect(ok: boolean, what: string): void {
    if (!ok)
        throw new Error(`expected ${what}`);
}

const kPool = "Pool/Default/Default_Stub";

await until(() => park.loaded(), 1200, "the park");
await frames(10);
park.setWeather("sunny");
park.setTemperature(28);
park.setTime(0.5);
const swimmers = () => people.list().filter((p) => p.state === "swimming");
await until(() => swimmers().length >= 3, 6000, "guests swimming");

// Everyone in a pool in their swimwear, nobody outside one.
const dressed = (state: string) => people.list().filter((p) => !p.staff && p.state === state);
expect(swimmers().every((p) => p.swimwear), "every swimmer in their swimwear");
const walking_in_swimwear = dressed("walking").filter((p) => p.swimwear).length;
expect(walking_in_swimwear === 0, `nobody walking the paths in swimwear, not ${walking_in_swimwear}`);

// Close up, from the side and from above: whole people in the water, not heads alone.
const water = map.objects().filter((o) => o.overlay === kPool && o.symbol === "Base");
const inWater = swimmers().filter((p) => water.some((w) => Math.floor(p.col!) === w.col && Math.floor(p.row!) === w.row));
log("swimming", swimmers().length, "in the water", inWater.length);
for (const [k, at] of inWater.slice(0, 2).entries()) {
    camera.look({ col: at.col!, row: at.row!, up: at.up! - 0.3 }, { col: at.col! + 2.2, row: at.row! + 0.4, up: at.up! + 0.6 });
    await frames(6);
    await screenshot(`pool_outfits_side_${k}`);
    camera.look({ col: at.col!, row: at.row!, up: at.up! }, { col: at.col! + 0.6, row: at.row! + 0.6, up: at.up! + 4 });
    await frames(6);
    await screenshot(`pool_outfits_above_${k}`);
}

/** Finds the pick-up button wherever the window holds it. */
const button = (path: string): string | undefined => {
    for (const child of ui.children(path) ?? []) {
        const at = `${path}/${child}`;
        if (child === "PickUp")
            return at;
        const found = button(at);
        if (found !== undefined)
            return found;
    }
    return undefined;
};
const pickUp = async (who: number) => {
    people.show(who);
    await until(() => button("info") !== undefined, 120, "the pick-up button");
    await ui.click(button("info")!);
    await until(() => people.list()[who].state === "held", 60, "the guest held");
};
/** Hangs the held guest over (col, row), a while for a look, then lets them go there. */
const letGo = async (col: number, row: number, name: string) => {
    const at = map.screen(col, row);
    expect(at !== undefined, `${col},${row} in sight`);
    pointer.move(at!.x, at!.y);
    await frames(10);
    await screenshot(name);
    await pointer.click(at!.x, at!.y);
};
/** The puffs of sparkles alive round the guest. */
const puffs = (who: number) => {
    const at = people.list()[who];
    return map.effects().filter((e) => e.effect === "magicsparkles" && Math.abs(e.col - at.col!) + Math.abs(e.row - at.row!) < 1.5);
};
/** A close look at the guest, from a little above. */
const closeUp = async (who: number, name: string) => {
    const at = people.list()[who];
    camera.look({ col: at.col!, row: at.row!, up: at.up! + 0.5 }, { col: at.col! + 2.5, row: at.row! + 1, up: at.up! + 2.5 });
    await frames(3);
    await screenshot(name);
};

// A guest on a path near the pool, picked up and let go over its water, close up.
const middle = water[Math.floor(water.length / 2)];
const paths = map.paths().filter((p) => !p.queue);
const distance = (a: { col?: number; row?: number }, b: { col: number; row: number }) => Math.abs(a.col! - b.col) + Math.abs(a.row! - b.row);
await until(() => dressed("walking").some((p) => distance(p, middle) < 25), 6000, "a guest walking near the pool");
const list = people.list();
const who = list.findIndex((p) => !p.staff && p.state === "walking" && distance(p, middle) < 25);
const pool = water.reduce((a, b) => (distance(list[who], b) < distance(list[who], a) ? b : a));
const near = (p: { col?: number; row?: number }) => distance(p, pool);
log("guest", list[who].name, "at", list[who].col, list[who].row, "pool square", pool.col, pool.row);
camera.look({ col: pool.col + 0.5, row: pool.row + 0.5 }, { col: pool.col + 4, row: pool.row + 4, up: 6 });
await frames(10);
await pickUp(who);
expect(people.list()[who].swimwear !== true, "the guest in their clothes, held");
await letGo(pool.col + 0.5, pool.row + 0.5, "pool_outfits_held_over_pool");
await until(() => people.list()[who].state === "swimming", 300, "the guest in the pool");
await frames(2);
expect(puffs(who).length > 0, "a puff of sparkles where they changed");
await closeUp(who, "pool_outfits_puff_in");
expect(people.list()[who].swimwear === true, "the guest in their swimwear in the pool");
await frames(90);
await closeUp(who, "pool_outfits_swimming_after");

// Out again, onto a path: in their swimwear while held, their clothes once down.
camera.look({ col: pool.col + 0.5, row: pool.row + 0.5 }, { col: pool.col + 4, row: pool.row + 4, up: 6 });
await frames(10);
await pickUp(who);
expect(people.list()[who].swimwear === true, "the guest still in their swimwear, held");
const path = paths.filter((p) => map.screen(p.col + 0.5, p.row + 0.5) !== undefined).sort((a, b) => near(a) - near(b))[0];
expect(path !== undefined, "a path in sight");
await letGo(path.col + 0.5, path.row + 0.5, "pool_outfits_held_over_path");
await until(() => people.list()[who].swimwear !== true, 300, "the guest dressed again");
await frames(2);
expect(puffs(who).length > 0, "a puff of sparkles where they changed back");
await closeUp(who, "pool_outfits_puff_out");
await frames(90);
await closeUp(who, "pool_outfits_dressed_after");
