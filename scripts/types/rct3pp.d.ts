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
    /** The names of the shown widgets in the one at `path`, in order; undefined when it is not shown. */
    function children(path: string): string[] | undefined;
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

interface FlatRideItem {
    overlay: string;
    symbol: string;
    /** String key of its name. */
    name: string;
    /** Tiles along its own X and Z. */
    width: number;
    length: number;
    /** Hundredths of the currency. */
    price: number;
    /** Whether it has an entrance and an exit to place. */
    doors: boolean;
}

declare namespace build {
    /** The install's rides placed whole; undefined while the lists are still being read. */
    function flatRides(): FlatRideItem[] | undefined;
    /** Starts placing a ride, as picking it in its build list does; false when there is no such ride. A ride
     * built piece by piece opens its construction panel on the next frame. */
    function pick(overlay: string, symbol: string): boolean;
    /** The construction panel's ride (its place in rides.list()), how many pieces fit where the track goes on,
     * the one picked and whether it can be built there; undefined while the panel is shut. */
    function construction(): { ride: number; offered: number; piece: string | null; ghost: boolean } | undefined;
    /** Puts the placing tool away, as a right click does. */
    function stop(): void;
    /** What is being placed, and what keeps it from where the pointer is; undefined while nothing is. */
    function placing(): { step: "ride" | "entrance" | "exit" | "scenery"; problem: "none" | "not owned" | "path" | "blocked"; affordable: boolean } | undefined;
}

/** A place a ride's part stands: its lowest tile, and the quarter turn it faces. */
interface Spot {
    col: number;
    row: number;
    direction: number;
}

interface RideInfo {
    name: string;
    /** With its tag, as "Twist:anr". */
    symbol: string;
    overlay: string;
    flat: boolean;
    /** Rides with a track: how many pieces it has. */
    pieces?: number;
    /** Rides with a track: the platform pieces of its stations, in track order. */
    platforms?: string[];
    /** Rides with a track: each piece's arm angles in degrees, base first, or null where it holds none. */
    arms?: (number[] | null)[];
    site: Spot | null;
    entrance: Spot | null;
    exit: Spot | null;
    /** Whether the simulation runs it. The rest are there only when it does. */
    joined: boolean;
    customers?: number;
    /** Hundredths of the currency. */
    price?: number;
    open?: boolean;
    /** Whether its entrance was found on the paths. */
    onPaths?: boolean;
    capacity?: number;
    cycleSeconds?: number;
    queue?: number;
    queueLine?: number;
    riders?: number;
    /** Excitement, intensity and nausea, 0..1. */
    ratings?: [number, number, number];
}

declare namespace rides {
    function list(): RideInfo[];
    /** Puts ride `i` of list() in `state`, as its window's open-or-close tab does. Null once changed, else the
     *  string key of why the ride stays as it was (a track without a station, a missing exit...). */
    function setState(i: number, state: "closed" | "testing" | "open"): string | null;
    /** Opens ride `i`'s window, as clicking the ride does, on the next frame. */
    function show(i: number): void;
}
