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
    /** Holds the left button down, or the right one with "right". */
    function press(button?: "right"): void;
    function release(button?: "right"): void;
    /** Holds the right or the middle button down, or lets it go. */
    function button(which: "right" | "middle", held: boolean): void;
    /** Holds Shift down with the pointer, or lets it go. */
    function shift(on: boolean): void;
    /** Holds Control down with the pointer, or lets it go. */
    function control(on: boolean): void;
    /** Turns the wheel `notches` for one frame, positive away from the user. */
    function wheel(notches: number): void;
    /** Moves to (x, y), presses and lets go; the right button with "right". */
    function click(x: number, y: number, button?: "right"): Promise<void>;
    /** Presses at `from`, moves to `to` over `count` frames and lets go; the right button with "right". */
    function drag(from: [number, number], to: [number, number], count?: number, button?: "right"): Promise<void>;
}

declare namespace keys {
    /** Presses the keys written as "Ctrl+V", "Shift+Left" or "Enter" for a frame, through the user's key bindings. */
    function press(combo: string): void;
    /** Types `text` for a frame. */
    function type(text: string): void;
}

declare namespace clipboard {
    /** The system clipboard's text, "" when it holds none. */
    function get(): string;
    function set(text: string): void;
}

declare namespace ui {
    /** The middle of the widget at `path` (such as "tools/Land/Land") and its edges, or undefined when it is not shown;
     * `on` for a toggle button or a tick box, `text` for an edit box or a label. */
    function find(path: string): { x: number; y: number; left: number; top: number; right: number; bottom: number; on?: boolean; text?: string } | undefined;
    /** Moves the pointer to the widget at `path`, presses and lets go. Throws when it is not shown. */
    function click(path: string): Promise<void>;
    /** Clicks the switch at `path` unless it is already `on`. */
    function toggle(path: string, on: boolean): Promise<void>;
    /** The names of the shown widgets in the one at `path` ("" for the top), in order; undefined when it is not shown. */
    function children(path: string): string[] | undefined;
}

declare namespace career {
    /**
     * Starts scenario `index` of the first career of `game` (0 the original, the default), made when there is none,
     * as its map's New does. park.ready() waits for the park.
     */
    function start(index: number, game?: number): void;
    /** Whether the career map is shown, with no park open. */
    function map(): boolean;
}

declare namespace render {
    /** What is alive on the device as the last frame ended. */
    function live(): { buffers: number; pipelines: number; textures: number };
}

declare namespace park {
    /** Opens the park at `path` in the game's folder, such as "Campaigns/Base/BlankLandscape.dat", and waits until it shows. */
    function open(path: string): Promise<void>;
    function loaded(): boolean;
    /** Waits until a park is open. */
    function ready(): Promise<void>;
    /** Hundredths of the currency; undefined in a park without money. */
    function cash(): number | undefined;
    /** Sets the cash, in hundredths of the currency, of a park with money. */
    function setCash(hundredths: number): void;
    /** Guests in the park now. */
    function guests(): number;
    /** Brings `parties` parties of guests in at once, each on a path tile of the park picked at random from
     *  `seed` (1 by default), as if they had walked there from the gate. Where each landed and how many it holds. */
    function dropParties(parties: number, seed?: number): { col: number; row: number; guests: number }[];
    /** The things guests use on the paths: benches, bins, signs and sights, by the placed object each stands as. */
    function pathItems(): { object: number; kind: "bin" | "bench" | "sign" | "sight" | "photo spot"; broken: boolean }[];
    /** Breaks every bench, bin and sign at once, as vandals would; how many it broke. */
    function breakPathItems(): number;
    /** Sets a handyman walking from the path on tile `col`, `row`, who is not drawn; whether there was a path. */
    function addHandyman(col: number, row: number): boolean;
    /** Posts a message with the text `key`, about ride `ride` of rides.list() if given, naming tab number `tab`
     *  for its camera to open (as saved messages number them). */
    function postMessage(key: string, ride?: number, tab?: number): void;
    /** The park's messages, oldest first: each one's text key and the name it was posted about. */
    function messages(): { key: string; name: string }[];
    /** The weather now, as "sunny", "cloudy", "light rain", "heavy rain", "thunderstorm" and the rest. */
    function weather(): string;
    /** Sets the weather now, by one of the names weather() gives. */
    function setWeather(name: string): void;
    /** How the weather shows now (rain and gloom 0 to 1, a lightning flash), the forecast, the climate's number and the temperature. */
    function weatherLook(): { rain: number; gloom: number; flash: number; forecast: string; climate: number; temperature: number };
    /** The calendar, in units of 90 a week and 360 a month. */
    function date(): number;
    /** Moves the calendar to `units`; the clock of the day runs on as it was. */
    function setDate(units: number): void;
    /** The scenario's VIP goals: their level, which celebrity, when they next visit, the months they stay,
     *  whether a visit went well and whether the player was told a month ahead. */
    function vipGoals(): { level: number; who: number; next: number; months: number; met: boolean; toldMonth: boolean }[];
    /** The VIPs who came in: name, where they are in tiles, patience 0 to 1, whether heading home or gone, and
     *  their place in people.list() once they are shown. */
    function vips(): { name: string; col: number; row: number; patience: number; leaving: boolean; gone: boolean; shown: number | null }[];
}

/** A place on the map: tiles (col 3.5 is the middle of column 3) and world units above the ground. */
interface MapPoint {
    col: number;
    row: number;
    up?: number;
}

declare namespace options {
    /** Sets the game option saved as `name` (such as "camera_mode") to `value`, kept in its range. */
    function set(name: string, value: number): void;
}

declare namespace camera {
    /** The overhead camera: radians it faces (0 along +Z) and looks down, how far back it is, and the point it
     *  looks at and its eye, in world units. */
    function view(): { yaw: number; pitch: number; distance: number; target: [number, number, number]; eye: [number, number, number] };
    /** Looks at `at` from `from`; without `from`, keeps the way it faces and how far back it is. */
    function look(at: MapPoint, from?: MapPoint): void;
    /** The water on the camera's lens: its drops and its sheets of water. */
    function lens(): { drops: number; sheets: number };
    /** Riding along in a car: its ride's place in rides.list(), the car (on a flat ride, the seat or fixed camera; going with a person, ride is -1 and car their place in
     *  people.list()), and once it
     *  is seen from, the rider's eyes in world units and the way they look. Undefined while the view is overhead. */
    function riding(): { ride: number; car: number; kind: "nose" | "front" | "rear" | "seat" | "fixed" | "guest" | "behind"; turn: [number, number]; eye?: [number, number, number]; ahead?: [number, number, number] } | undefined;
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
    /** The animal enclosure it is in, by number; -1 when in none. */
    enclosure: number;
    /** The surface height of the water on it, when it holds any. */
    water?: number;
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
    /** The middle of a tile on the view (on a pool square, at its surface), or with fractions that point of it;
     *  undefined when it is not in sight. */
    function screen(col: number, row: number): { x: number; y: number } | undefined;
    function paths(): PathPiece[];
    /** The tiles with support columns standing on them, and how many pieces each has. */
    function supports(): { col: number; row: number; pieces: number }[];
    /** The placed objects that are shown: scenery, rides' parts, supports, fences and the rest. */
    function objects(): { symbol: string; overlay: string; col: number; row: number }[];
    function water(): Lake[];
    /** The waterfalls: the middle of the edge each pours over and where its water lands, in tiles, the levels of
     *  the lakes it joins and how wide it is in world units. */
    function waterfalls(): { col: number; row: number; landCol: number; landRow: number; top: number; bottom: number; width: number }[];
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
    /** Whether it stands only on water. */
    water: boolean;
    /** The fence it takes when placed, its theme's; empty when none is named. */
    fence: string;
}

interface StallItem {
    overlay: string;
    symbol: string;
    /** String key of its name. */
    name: string;
    width: number;
    length: number;
    /** Hundredths of the currency. */
    price: number;
    /** What a new one sells: each carried item's name and the price it starts at, in hundredths. */
    goods: { item: string; price: number }[];
}

declare namespace build {
    /** The install's rides placed whole; undefined while the lists are still being read. */
    function flatRides(): FlatRideItem[] | undefined;
    /** The install's stalls; undefined while the lists are still being read. Picked as rides are. */
    function stalls(): StallItem[] | undefined;
    /** Starts placing a ride; false when there is no such ride. A ride built piece by piece opens its
     * construction panel on the next frame, for a custom track; with "designs" it also opens the window of its
     * kind's saved designs ("design_browser"), as picking it in its build list does when it has some. An
     * enclosure fence, or the enclosure list's delete tool ("", "EnclosureDelete"), takes out the enclosure tool. */
    function pick(overlay: string, symbol: string, offer?: "designs"): boolean;
    /** The construction panel's ride (its place in rides.list()), how many pieces fit where the track goes on,
     * the one picked, whether it can be built there (and its arrows show), whether it may not stand there (for a design,
     * any of its pieces or doors) and whether for land the park does not own, the doors a design's ghost shows, and the files of the saved designs of its kind (the
     * player's own, then the install's), the piece clicked mid-track to build over (its place in the ride's
     * track), null while building at the open end, and on a ride of two tracks side by side the split button's
     * state: 1 joined from the left track, 2 the left alone, 3 joined from the right, 4 the right alone; 0 on
     * other rides. Undefined while the panel is shut. */
    function construction(): { ride: number; offered: number; piece: string | null; ghost: boolean; arrows: boolean; ghostAt: { col: number; row: number; height: number } | null; clashes: boolean; unowned: boolean; ghostDoors: number; completing: boolean; proposed: number; complaint: string; testing: boolean; testAt: number | null; testWrecks: number; designs: string[]; selected: number | null; split: number } | undefined;
    /** Puts the placing tool or the enclosure tool away, as a right click does. */
    function stop(): void;
    /** While an enclosure drag is held: what letting go would cost in hundredths (below 0 when it pays back),
     * and how many tiles it would take in or out; undefined otherwise. */
    function enclosing(): { cost: number; tiles: number } | undefined;
    /** What is being placed, what keeps it from where the pointer is, and whether a stall's facing arrow shows;
     * undefined while nothing is. */
    function placing(): { step: "ride" | "entrance" | "exit" | "scenery"; problem: "none" | "not owned" | "path" | "blocked" | "off water" | "under water" | "off path" | "off enclosure"; affordable: boolean; ghost: boolean; arrow: boolean; site?: { col: number; row: number; turns: number; height: number } } | undefined;
}

/** A place a ride's part stands: its lowest tile, and the quarter turn it faces. */
interface Spot {
    col: number;
    row: number;
    direction: number;
}

interface RideInfo {
    name: string;
    /** The fence round it, without its tag; empty when it has none. */
    fence: string;
    /** With its tag, as "Twist:anr". */
    symbol: string;
    overlay: string;
    flat: boolean;
    /** Rides with a track: how many pieces it has. */
    pieces?: number;
    /** Rides with a track: how many trains it has, and cars in each. */
    trains?: number;
    carsPerTrain?: number;
    /** Rides with a track: passenger cars in each train, without the links between them. */
    passengerCars?: number;
    /** Ridden by the park: how many of the simulation's attractions are this ride (1 unless taken on twice). */
    simulated?: number;
    /** Ridden by the park on a track: the trains it runs, and how many are under way now. */
    running?: number;
    moving?: number;
    /** Rides with a track: each piece's name, in track order. */
    track?: string[];
    /** Rides with a track: the platform pieces of its stations, in track order. */
    platforms?: string[];
    /** Rides with a track: each piece's arm angles in degrees, base first, or null where it holds none. */
    arms?: (number[] | null)[];
    /** Rides with a track: where each of its cars stands now. */
    cars?: MapPoint[];
    site: Spot | null;
    entrance: Spot | null;
    exit: Spot | null;
    /** Whether it was pulled down; it keeps its place in the list. */
    demolished: boolean;
    /** Samples in its graphs: its last lap's, a tenth of a second each. */
    graphSamples: number;
    /** Whether the simulation runs it. The rest are there only when it does. */
    joined: boolean;
    customers?: number;
    /** Hundredths of the currency. */
    price?: number;
    open?: boolean;
    /** Whether its entrance was found on the paths. */
    onPaths?: boolean;
    /** Where the guests walking to it are now. */
    heading?: { col: number; row: number }[];
    capacity?: number;
    cycleSeconds?: number;
    queue?: number;
    queueLine?: number;
    riders?: number;
    /** Excitement, intensity and nausea, 0..1. */
    ratings?: [number, number, number];
    /** Rides whose trains have crashed: how often, and where the wrecks of the crashed trains are now. */
    crashes?: number;
    wrecks?: (MapPoint & { state: "flying" | "burning" | "gone" })[];
}

declare namespace rides {
    function list(): RideInfo[];
    /** Puts ride `i` of list() in `state`, as its window's open-or-close tab does. Null once changed, else the
     *  string key of why the ride stays as it was (a track without a station, a missing exit...). */
    function setState(i: number, state: "closed" | "testing" | "open"): string | null;
    /** Opens ride `i`'s window, as clicking the ride does, on the next frame. */
    function show(i: number): void;
    /** The ride the info panel shows and its tab, by the tab's help key; undefined when it shows no ride. */
    function shown(): { ride: number; tab?: string } | undefined;
}

interface StallInfo {
    name: string;
    /** With its tag, as "BurgerStall:sta". */
    symbol: string;
    overlay: string;
    site: Spot | null;
    open: boolean;
    /** What it sells, in its menu's order: each item's symbol with its tag, as "Burger:cid", its price in
     *  hundredths, its name as kept ("%ME%" for the item's own) and which of its extras go on, a bit each. */
    items: { item: string; price: number; name: string; extras: number }[];
    /** How much of each extra it puts on: 0 none up to 4 loads, 5 as the guest picks. */
    extras: { extra: string; level: number }[];
    /** Whether changes to its menu go to every stall of its kind. */
    sync: boolean;
    /** Whether the info panel shows it. */
    shown: boolean;
    /** Whether it was pulled down; it keeps its place in the list. */
    demolished: boolean;
    /** Whether the simulation runs it, once a path runs past the tile it faces. The rest are there only when
     * it does: the price it sells at, in hundredths, and whether it is open to guests there. */
    joined: boolean;
    price?: number;
    selling?: boolean;
}

declare namespace stalls {
    function list(): StallInfo[];
    /** Opens stall `i` of list() in the info panel, as a click on it does. */
    function show(i: number): void;
    /** Puts stall `i` of list() in `state`, as its window's open-or-close tab does. Null once changed, else the
     *  string key of why it stays as it was (nothing on its menu). */
    function setState(i: number, state: "closed" | "open"): string | null;
}

declare namespace people {
    /** The people shown in the park, guests and staff, in the order the park keeps them, with the point of the
     *  map they stand on, in tiles. For those the simulation walks, also where they are (tiles, and world height
     *  `up`) and what they are at ("waiting": for the rest of their party, standing with the others; "buying":
     *  at a stall's counter while they wait; "thrown": by a crash, still in the air), how they go on foot
     *  (`gait`) and, seated on a coaster, how they react to it. A seller has the id of the object of the stall
     *  or ride entrance they serve at as `post`. */
    function list(): { name: string; staff: boolean; post?: number; at: { col: number; row: number } | null; col?: number; row?: number; up?: number; state?: "walking" | "queueing" | "riding" | "waiting" | "buying" | "held" | "dropped" | "thrown" | "still"; gait?: "walk" | "run" | "weary" | "queasy" | "bursting"; reaction?: "waiting" | "calm" | "hands up" | "covering eyes" | "strained" }[];
    /** Opens the window of person `i` of list() on the next frame, as a click on them does. */
    function show(i: number): void;
    /** Renames person `i` of list() on the next frame, as typing in their window's name box does. */
    function rename(i: number, name: string): void;
}

declare namespace structures {
    /** The structure being placed, while the placing tool is out: what its ghost shows and whether it may go there. */
    function placing(): { name: string; items: number; problem: "none" | "not owned" | "blocked" | "path"; affordable: boolean; cost: number; turns: number; height: number; ghost: boolean; at?: { col: number; row: number } } | undefined;
    /** How many tiles the picking out tool has marked. */
    function marked(): number;
}

declare namespace animals {
    /** The park's animals, each with a number of its own, its species, the enclosure they live in (-1 for none), whether they are in a
     *  crate or out of every enclosure, their health from 0 to 1, and the point of the map they stand on, in
     *  tiles. */
    function list(): { id: number; species: string; enclosure: number; crated: boolean; escaped: boolean; health: number; at: { col: number; row: number } }[];
}
