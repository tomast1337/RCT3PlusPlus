// SPDX-License-Identifier: GPL-3.0-or-later
//
// What rct3++ gives scripts run with `rct3pp --script FILE.js` (app/scene/script_host.hpp). Positions are
// shares of the game's view, 0 to 1 from the top-left corner.

/** Waits `count` frames (1 when left out). */
declare function frames(count?: number): Promise<void>;
/** Waits frame by frame until `check` returns true; throws, naming `what`, after `limit` frames (300). */
declare function until(check: () => boolean, limit?: number, what?: string): Promise<void>;
/** Writes its arguments to the game's log, under "script". */
declare function log(...values: unknown[]): void;
/** Saves a screenshot, named `name` in the script's report; resolves once it is written. */
declare function screenshot(name?: string): Promise<void>;
/** Leaves the game with `code` (0 when left out). */
declare function quit(code?: number): void;

declare namespace pointer {
    function move(x: number, y: number): void;
    function press(): void;
    function release(): void;
    /** Holds Shift down with the pointer, or lets it go. */
    function shift(on: boolean): void;
    /** Holds Control down with the pointer, or lets it go. */
    function control(on: boolean): void;
    /** Moves to (x, y), presses and lets go. */
    function click(x: number, y: number): Promise<void>;
    /** Presses at `from`, moves to `to` over `count` frames and lets go. */
    function drag(from: [number, number], to: [number, number], count?: number): Promise<void>;
}

declare namespace ui {
    /** The middle of the widget at `path` (such as "tools/Land/Land"), or undefined when it is not shown. */
    function find(path: string): { x: number; y: number; on?: boolean } | undefined;
    /** Moves the pointer to the widget at `path`, presses and lets go. Throws when it is not shown. */
    function click(path: string): Promise<void>;
    /** Clicks the switch at `path` unless it is already `on`. */
    function toggle(path: string, on: boolean): Promise<void>;
}

declare namespace park {
    /** Opens the park at `path` in the game's folder, such as "Campaigns/Base/BlankLandscape.dat", and waits until it shows. */
    function open(path: string): Promise<void>;
    function loaded(): boolean;
    /** Waits until a park is open. */
    function ready(): Promise<void>;
    /** Hundredths of the currency; undefined in a park without money. */
    function cash(): number | undefined;
}

/** A place on the map: tiles (col 3.5 is the middle of column 3) and world units above the ground. */
interface MapPoint {
    col: number;
    row: number;
    up?: number;
}

declare namespace camera {
    /** Looks at `at` from `from`; without `from`, keeps the way it faces and how far back it is. */
    function look(at: MapPoint, from?: MapPoint): void;
}

interface Tile {
    /** Heights of the corners: (col, row), (col + 1, row), (col, row + 1), (col + 1, row + 1). */
    corners: [number, number, number, number];
    /** Index of the land surface it is painted with. */
    surface: number;
    /** Index of the cliff face its cliffs are painted with. */
    cliff: number;
    /** Whether the park owns it outright. */
    owned: boolean;
}

interface PathPiece {
    col: number;
    row: number;
    /** Off the ground, on supports. */
    raised: boolean;
    queue: boolean;
    /** Of a raised piece, in the game's height steps. */
    height?: number;
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
    /** The middle of a tile on the view; undefined when it is not in sight. */
    function screen(col: number, row: number): { x: number; y: number } | undefined;
    function paths(): PathPiece[];
    function water(): Lake[];
}
