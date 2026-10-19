// SPDX-License-Identifier: GPL-3.0-or-later
//
// What rct3++ gives scripts run with `rct3pp --script FILE.js` (app/scene/script_host.hpp). Positions are
// shares of the game's view, 0 to 1 from the top-left corner.

/** Waits `count` frames (1 when left out). */
declare function frames(count?: number): Promise<void>;
/** Writes its arguments to the game's log, under "script". */
declare function log(...values: unknown[]): void;
/** Saves a screenshot to the screenshots folder; resolves once it is written. */
declare function screenshot(): Promise<void>;
/** Leaves the game with `code` (0 when left out). */
declare function quit(code?: number): void;

declare namespace pointer {
    function move(x: number, y: number): void;
    function press(): void;
    function release(): void;
    /** Presses at `from`, moves to `to` over `count` frames and lets go. */
    function drag(from: [number, number], to: [number, number], count?: number): Promise<void>;
}

declare namespace ui {
    /** The middle of the widget at `path` (such as "tools/Land/Land"), or undefined when it is not shown. */
    function find(path: string): { x: number; y: number } | undefined;
    /** Moves the pointer to the widget at `path`, presses and lets go. Throws when it is not shown. */
    function click(path: string): Promise<void>;
}

declare namespace park {
    function loaded(): boolean;
    /** Waits until a park is open. */
    function ready(): Promise<void>;
    /** Hundredths of the currency; undefined in a park without money. */
    function cash(): number | undefined;
}

interface Tile {
    /** Heights of the corners: (col, row), (col + 1, row), (col, row + 1), (col + 1, row + 1). */
    corners: [number, number, number, number];
    /** Index of the land surface it is painted with. */
    surface: number;
    /** Whether the park owns it outright. */
    owned: boolean;
}

interface Lake {
    level: number;
    /** Half-tiles it covers. */
    triangles: number;
}

declare namespace map {
    function size(): { width: number; height: number };
    /** Undefined off the map. */
    function tile(col: number, row: number): Tile | undefined;
    function water(): Lake[];
}
