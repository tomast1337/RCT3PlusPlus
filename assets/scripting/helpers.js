globalThis.until = async (check, limit = 300, what = "the wait") => {
    for (let i = 0; i < limit; ++i) {
        if (check()) return;
        await frames(1);
    }
    if (!check()) throw new Error(`${what} timed out after ${limit} frames`);
};
pointer.drag = async (from, to, count = 10, button) => {
    pointer.move(from[0], from[1]);
    await frames(1);
    pointer.press(button);
    for (let i = 1; i <= count; ++i) {
        await frames(1);
        pointer.move(from[0] + ((to[0] - from[0]) * i) / count, from[1] + ((to[1] - from[1]) * i) / count);
    }
    await frames(1);
    pointer.release(button);
    await frames(1);
};
pointer.click = async (x, y, button) => {
    pointer.move(x, y);
    await frames(1);
    pointer.press(button);
    await frames(1);
    pointer.release(button);
    await frames(1);
};
ui.toggle = async (path, on) => {
    const at = ui.find(path);
    if (!at) throw new Error(`no widget ${path} is shown`);
    if (at.on !== on) await ui.click(path);
};
ui.click = async (path) => {
    // Waits for the widget to stop moving, as menus slide open.
    let at = ui.find(path);
    for (let i = 0; i < 60; ++i) {
        await frames(1);
        const now = ui.find(path);
        if (now && at && now.x === at.x && now.y === at.y) break;
        at = now;
    }
    if (!at) throw new Error(`no widget ${path} is shown`);
    pointer.move(at.x, at.y);
    await frames(1);
    pointer.press();
    await frames(1);
    pointer.release();
    await frames(1);
};
{
    // Read from the screen as the frame ends and written a frame later.
    const take = screenshot;
    globalThis.screenshot = async (name = "shot") => {
        take(name);
        await frames(3);
    };
}
{
    // The park asked for loads as the frame ends.
    const open = park.open;
    park.open = async (path, settings) => {
        open(path, settings ?? {});
        await frames(1);
        await park.ready();
    };
}
park.ready = async () => {
    while (!park.loaded()) await frames(1);
};
{
    // The park runs on as the frame starts, before it is drawn.
    const run = park.advance;
    park.advance = async (seconds) => {
        run(seconds);
        await frames(1);
    };
}
park.advanceUntil = async (check, seconds = 60, what = "the wait") => {
    for (let i = 0; i < seconds; ++i) {
        if (check()) return;
        await park.advance(1);
    }
    if (!check()) throw new Error(`${what} timed out after ${seconds} park seconds`);
};
// What the test scripts share: checks, widget lookups and looks at the map.
globalThis.expect = (ok, what) => {
    if (!ok) throw new Error(`expected ${what}`);
};
globalThis.must = (value, what) => {
    if (value === undefined || value === null) throw new Error(`expected ${what}`);
    return value;
};
globalThis.under = (path, name) => {
    for (const child of ui.children(path) ?? []) {
        const at = `${path}/${child}`;
        if (child === name) return at;
        const found = under(at, name);
        if (found !== undefined) return found;
    }
    return undefined;
};
globalThis.on_view = (col, row) => {
    const at = map.screen(col, row);
    if (!at) throw new Error(`tile ${col}, ${row} is not in sight`);
    return [at.x, at.y];
};
globalThis.look_down = async (col, row, up = 30) => {
    camera.look({ col: col + 0.5, row: row + 0.5 }, { col: col + 0.5, row: row + 0.5 + up / 3, up });
    await frames(3);
};
globalThis.click_tool = async (tool) => ui.click(`tools/${tool}/${ui.children(`tools/${tool}`)[0]}`);
globalThis.files_menu = async (item) => {
    if (ui.find(`tools/Files/${item}`) === undefined) await ui.click(`tools/Files/${ui.children("tools/Files")[0]}`);
    await until(() => ui.find(`tools/Files/${item}`) !== undefined, 120, "the files menu");
    await ui.click(`tools/Files/${item}`);
};
globalThis.info_tab = (part) => ui.children("info").find((t) => t.toLowerCase().includes(part));
globalThis.has_path = (col, row) => map.paths().some((p) => p.col === col && p.row === row);
globalThis.staff = () => people.list().filter((p) => p.staff);
globalThis.swimmers = () => people.list().filter((p) => p.state === "swimming");
