globalThis.until = async (check, limit = 300, what = "the wait") => {
    for (let i = 0; i < limit; ++i) {
        if (check()) return;
        await frames(1);
    }
    if (!check()) throw new Error(`${what} timed out after ${limit} frames`);
};
pointer.drag = async (from, to, count = 10) => {
    pointer.move(from[0], from[1]);
    await frames(1);
    pointer.press();
    for (let i = 1; i <= count; ++i) {
        await frames(1);
        pointer.move(from[0] + (to[0] - from[0]) * i / count, from[1] + (to[1] - from[1]) * i / count);
    }
    await frames(1);
    pointer.release();
    await frames(1);
};
pointer.click = async (x, y) => {
    pointer.move(x, y);
    await frames(1);
    pointer.press();
    await frames(1);
    pointer.release();
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
    park.open = async (path) => {
        open(path);
        await frames(1);
        await park.ready();
    };
}
park.ready = async () => {
    while (!park.loaded()) await frames(1);
};
