// SPDX-License-Identifier: GPL-3.0-or-later
//
// What rct3++ gives scripts run with `rct3pp --script FILE.js` (app/scene/script_host.hpp). Positions are
// shares of the game's view, 0 to 1 from the top-left corner.
//
// Branded numbers: tsc checks the brand and erases it, so scripts still run on plain numbers.
// - Places in a list (RideIndex, StallIndex, PersonIndex) and placed objects' ids (PlacedObjectId) are strict: take
//   them from the API (each list item's `index`, pathItems()' `object`), or cast a literal (`3 as RideIndex`), which
//   marks it as a magic number.
// - Units (Money in hundredths, Tiles, WorldUnits, Seconds, Frames) are loose: plain numbers and sums pass, but a
//   value the API gave in one unit is refused where another is meant.

/** A ride's place in rides.list(), as its `index`. */
type RideIndex = number & { readonly __brand: "RideIndex" };
/** A stall's place in stalls.list(), as its `index`. */
type StallIndex = number & { readonly __brand: "StallIndex" };
/** A person's place in people.list(), as their `index`. */
type PersonIndex = number & { readonly __brand: "PersonIndex" };
/** A placed object's id, as park.pathItems() gives it. */
type PlacedObjectId = number & { readonly __brand: "PlacedObjectId" };
/** Hundredths of the currency. */
type Money = number & { readonly __unit?: "Money" };
/** Tiles across the map: col 3.5 is the middle of column 3. */
type Tiles = number & { readonly __unit?: "Tiles" };
/** World units, as heights above the ground and the camera's points are given. */
type WorldUnits = number & { readonly __unit?: "WorldUnits" };
/** Seconds of the park's own time. */
type Seconds = number & { readonly __unit?: "Seconds" };
/** Frames drawn. */
type Frames = number & { readonly __unit?: "Frames" };

/** Waits `count` frames (1 when left out). */
declare function frames(count?: Frames): Promise<void>;
/** Waits frame by frame until `check` returns true; throws, naming `what`, after `limit` frames (300). */
declare function until(check: () => boolean, limit?: Frames, what?: string): Promise<void>;
/** Writes its arguments to the game's log, under "script". */
declare function log(...values: unknown[]): void;
/** Saves a screenshot, named `name` in the script's report; resolves once it is written. */
declare function screenshot(name?: string): Promise<void>;
/** The script's own arguments: those after a second `--`, as in `./nob script FILE -- --no-audio -- --strict`. */
declare function args(): string[];
/** Writes `text` to the file named `file` in the script's report; false when the run has no report. */
declare function report(file: string, text: string): boolean;
/** Leaves the game with `code` (0 when left out). */
declare function quit(code?: number): void;

/** Throws "expected `what`" unless `ok`. */
declare function expect(ok: boolean, what: string): void;
/** `value`, or throws "expected `what`" when it is undefined or null: for a lookup the script relies on. */
declare function must<T>(value: T | undefined | null, what: string): T;
/** The path of the first widget named `name` anywhere below the one at `path`, or undefined. */
declare function under(path: string, name: string): string | undefined;
/** Where the middle of tile (col, row) is on screen; throws when it is not in sight. */
declare function on_view(col: Tiles, row: Tiles): [number, number];
/** Points the camera down at tile (col, row) from the south, `up` (30) above it, and waits for it to draw. */
declare function look_down(col: Tiles, row: Tiles, up?: WorldUnits): Promise<void>;
/** Clicks the first button of the tool bar menu `tool` (such as "Land"), which opens or closes it. */
declare function click_tool(tool: string): Promise<void>;
/** Clicks `item` in the files menu, opening the menu first when it is shut. */
declare function files_menu(item: string): Promise<void>;
/** The tab of the open info window whose name holds `part` (lower case), or undefined. */
declare function info_tab(part: string): string | undefined;
/** Whether a path lies on tile (col, row). */
declare function has_path(col: Tiles, row: Tiles): boolean;
/** `person`, or throws "expected `what`" when it is missing or the simulation does not walk them. */
declare function walked<P extends Person>(
    person: P | undefined | null,
    what: string,
): Exclude<P, { state?: undefined }>;
/** `ride`, or throws "expected `what`" when it is missing or has no track. */
declare function tracked(ride: RideInfo | undefined | null, what: string): RideInfo & { kind: "tracked" };
/** The staff among people.list(). */
declare function staff(): Extract<Person, { staff: true }>[];
/** The people swimming, among people.list(). */
declare function swimmers(): (Person & { state: "swimming" })[];

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
    function drag(from: [number, number], to: [number, number], count?: Frames, button?: "right"): Promise<void>;
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
     * `on` for a toggle button or a tick box, `text` for an edit box, a label or a button with words, `tooltip` for one with help.
     * These stay optional: they follow the widget's kind, which no field gives. */
    function find(path: string):
        | {
              x: number;
              y: number;
              left: number;
              top: number;
              right: number;
              bottom: number;
              on?: boolean;
              text?: string;
              tooltip?: string;
          }
        | undefined;
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

/** What a path item is, as park.pathItems() and people.list() name it. */
type PathItemKind =
    "bin" | "bench" | "sign" | "sight" | "photo spot" | "fountain" | "water cannon" | "speaker" | "passport post";

/** The weathers a park has. */
type Weather = "sunny" | "sunny-cloudy" | "cloudy" | "light rain" | "heavy rain" | "thunderstorm" | "snowstorm";

declare namespace park {
    /** Opens the park at `path` in the game's folder, such as "Campaigns/Base/BlankLandscape.dat", and waits until it shows.
     *  It is played as a sandbox, which offers everything, unless `sandbox` is false. */
    function open(path: string, settings?: { sandbox?: boolean }): Promise<void>;
    function loaded(): boolean;
    /** Runs the park on `seconds` of its own time, whatever its speed, without drawing them: a step of its
     *  simulation at a time, as frames would run them; nothing while paused. Resolves a frame later, drawn as it is then. */
    function advance(seconds: Seconds): Promise<void>;
    /** Runs the park on a second of its time a frame until `check` returns true; throws, naming `what`, after
     *  `seconds` park seconds (60). */
    function advanceUntil(check: () => boolean, seconds?: Seconds, what?: string): Promise<void>;
    /** Waits until a park is open. */
    function ready(): Promise<void>;
    /** Hundredths of the currency; undefined in a park without money. */
    function cash(): Money | undefined;
    /** The loan, in hundredths of the currency. */
    function loan(): Money;
    /** Sets the cash, in hundredths of the currency, of a park with money. */
    function setCash(hundredths: Money): void;
    /** The building and research lines of the newest month's ledger, in hundredths: spending above zero, refunds below. */
    function ledger(): { landPurchase: Money; landscaping: Money; rideConstruction: Money; research: Money };
    /** Research: the monthly budget and what is counted towards the current item (hundredths), the current item and
     *  those invented since the original park, by symbol, with the calendar time each was invented (as park.date()),
     *  the item research is focused on, the cheapest few of each area that may come next, how many items are left
     *  (null until the build lists are read), and the priority of each research area from 0 to 1, in the research
     *  window's order (areas past the end weigh 1). */
    function research(): {
        budget: Money;
        spent: Money;
        current: string | null;
        invented: (string | null)[];
        inventedAt: number[];
        focus: string | null;
        next: (string | null)[];
        left: number | null;
        priorities: number[];
    };
    /** Focuses research on the item left to research named `symbol`, at once and at a higher cost, or with null ends
     *  the focus. False when no such item is left. */
    function focusResearch(symbol: string | null): boolean;
    /** Sets the monthly research budget, in hundredths of the currency; nought stops research. `priorities` weighs
     *  each research area in the research window's order, as research() gives them. */
    function setResearch(hundredths: Money, priorities?: number[]): void;
    /** The weekly graphs' samples, newest first (money in hundredths), each ledger period's sum newest first, and
     *  the month, counted from the calendar's start, that the newest period is for (null before anything is booked). */
    function history(): {
        cashLessLoan: Money[];
        companyValue: Money[];
        parkValue: Money[];
        income: Money[];
        expenditure: Money[];
        weeklyProfit: Money[];
        rating: number[];
        guests: number[];
        intake: number[];
        ledger: Money[];
        ledgerMonth: number | null;
    };
    /** Guests in the park now. */
    function guests(): number;
    /** The path tiles under the park's entrances, where guests come in and go out. */
    function gates(): { col: Tiles; row: Tiles }[];
    /** The path tiles of the park's spawn points, where arriving guests appear and those going home walk off. */
    function spawns(): { col: Tiles; row: Tiles }[];
    /** Brings `parties` parties of guests in at once, each on a path tile of the park picked at random from
     *  `seed` (1 by default), as if they had walked there from the gate. Where each landed and how many it holds. */
    function dropParties(parties: number, seed?: number): { col: Tiles; row: Tiles; guests: number }[];
    /** The things guests use on the paths: benches, bins, signs and sights, by the placed object each stands as. */
    function pathItems(): { object: PlacedObjectId; kind: PathItemKind; broken: boolean }[];
    /** The park's passport gift, which its passport posts' windows set: how many stamps a guest needs for it, the
     *  item given (by symbol, "" for none), and the items it may be (souvenirs). */
    function passport(): { stampsForGift: number; gift: string; gifts: string[] };
    /** Sets how many stamps a passport needs for the gift (1 to 12), and the gift, one of passport().gifts, unless
     *  left out; false, changing nothing, for a number or gift out of these. */
    function setPassport(stampsForGift: number, gift?: string): boolean;
    /** Opens the window of the placed object `object` (as pathItems() gives) on the next frame, as a click on it does. */
    function showObject(object: PlacedObjectId): void;
    /** The litter and sick lying on the paths, each by the middle of the path tile it lies on, in tiles. */
    function litter(): { col: Tiles; row: Tiles; sick: boolean }[];
    /** Breaks every bench, bin and sign at once, as vandals would; how many it broke. */
    function breakPathItems(): number;
    /** Sets the energy of every guest to `share` (0 to 1), as a long day would; how many guests. */
    function tireGuests(share: number): number;
    /** Sets the cash of every guest to `hundredths`, as a day of spending would; how many guests. */
    function setGuestCash(hundredths: Money): number;
    /** Drops `pieces` pieces of litter (1 to 50) on the path nearest tile `col`, `row`; whether there was a path. */
    function dropLitter(col: Tiles, row: Tiles, pieces: number): boolean;
    /** Empties every flower bed of its water; how many there are. */
    function dryFlowerBeds(): number;
    /** Sets a handyman walking from the path on tile `col`, `row`, who is not drawn; whether there was a path. */
    function addHandyman(col: Tiles, row: Tiles): boolean;
    /** Posts a message with the text `key`, about ride `ride` of rides.list() if given, naming tab number `tab`
     *  for its camera to open (as saved messages number them). */
    function postMessage(key: string, ride?: RideIndex, tab?: number): void;
    /** The park's messages, oldest first: each one's text key, the name it was posted about, and for an award or a
     *  fine, its number as the park file keeps them. Text keys come from the game's string tables, an open set, so
     *  `award` cannot be told apart by `key` here and stays optional. */
    function messages(): { key: string; name: string; award?: number }[];
    /** The weather now; "unknown" for a value the park file keeps outside the known ones. */
    function weather(): Weather | "unknown";
    /** Sets the weather now. */
    function setWeather(name: Weather): void;
    /** Sets the park's temperature, in degrees, which the weather then drifts on from. */
    function setTemperature(degrees: number): void;
    /** How the weather shows now (rain and gloom 0 to 1, a lightning flash), the forecast, the climate's number and the temperature. */
    function weatherLook(): {
        rain: number;
        gloom: number;
        flash: number;
        /** "" when the park has none. */
        forecast: Weather | "unknown" | "";
        climate: number;
        temperature: number;
    };
    /** Sets the time of day, as a fraction of a day from midnight (0.5 is noon), shown at once. */
    function setTime(fraction: number): void;
    /** Which part of the day the park keeps: its opening hours, or a window round the middle of the day or of
     *  the night, as the bar's day button sets it. The look gets there as the day transition option says. */
    function setDayMode(mode: "hours" | "day" | "night"): void;
    /** Where the park is in its day and what the sky shows (fractions of a day from midnight), and its mode. */
    function dayClock(): { time: number; shown: number; mode: "hours" | "day" | "night" };
    /** The calendar, in units of 90 a week and 360 a month. */
    function date(): number;
    /** Levels of the scenario won since the park opened, each heard as a fanfare. */
    function levelsWon(): number;
    /** Gives the park prize `id` as winning a level would, its box told like one won. */
    function givePrize(id: number): void;
    /** Moves the calendar to `units`; the clock of the day runs on as it was. */
    function setDate(units: number): void;
    /** The scenario's VIP goals: their level, which celebrity, when they next visit, the months they stay,
     *  whether a visit went well and whether the player was told a month ahead. */
    function vipGoals(): {
        level: number;
        who: number;
        next: number;
        months: number;
        met: boolean;
        toldMonth: boolean;
    }[];
    /** The VIPs who came in: name, where they are in tiles, patience 0 to 1, whether heading home or gone, and
     *  their place in people.list() once they are shown. */
    function vips(): {
        name: string;
        col: Tiles;
        row: Tiles;
        patience: number;
        leaving: boolean;
        gone: boolean;
        shown: PersonIndex | null;
    }[];
    /** The park's firework displays: name, what most of its shots are (a fountain show is mostly water jets), how
     *  long it runs, music file, the months they run in (a bit a month, the first lowest), start time in minutes
     *  after midnight, launchers and shots, and whether one plays now, how far it is and the middle of its
     *  launchers. */
    function displays(): {
        name: string;
        kind: "fireworks" | "lasers" | "water";
        length: Seconds;
        music: string;
        months: number;
        start: number;
        launchers: number;
        shots: number;
        playing: boolean;
        seconds: number;
        centre?: MapPoint;
    }[];
    /** The show rides' shows: the ride's place in rides.list() and name, the string keys of its tricks in the order
     *  the show puts them on and how long they take, whether a show runs now (it starts as the ride sets off), how
     *  far into it, and the trick going on (null between shows and after the last trick). */
    function shows(): {
        ride: RideIndex;
        name: string;
        tricks: string[];
        seconds: Seconds;
        running: boolean;
        into: Seconds;
        trick: string | null;
    }[];
    /** Starts display `index` at once, in place of any playing; false when there is no such display. */
    function startDisplay(index: number): boolean;
    function stopDisplay(): void;
}

/** A place on the map: tiles (col 3.5 is the middle of column 3) and world units above the ground. */
interface MapPoint {
    col: Tiles;
    row: Tiles;
    up?: WorldUnits;
}

declare namespace debug {
    /** Brings the developer windows' Debug tab to the front, once they are shown (keys.press("F12")). */
    function open(): void;
    /** Uses the Debug tab's tool `tool` with `amount`: money to add in hundredths, or how many guests or pieces of
     *  litter. Returns how many things it changed. */
    function use(
        tool:
            | "add_money"
            | "buy_all_land"
            | "finish_research"
            | "add_guests"
            | "remove_guests"
            | "refresh_guests"
            | "break_rides"
            | "reset_rides"
            | "remove_animals"
            | "refresh_animals"
            | "drop_litter"
            | "clear_litter"
            | "rebuild_waterfalls"
            | "reset_jets"
            | "unlock_designs"
            | "unlock_scenarios"
            | "reset_item_names",
        amount?: number,
    ): number;
}

declare namespace options {
    /** Sets the game option saved as `name` (such as "camera_mode") to `value`, kept in its range. */
    function set(name: string, value: number): void;
}

declare namespace camera {
    /** The overhead camera: radians it faces (0 along +Z) and looks down, how far back it is, and the point it
     *  looks at and its eye, in world units. */
    function view(): {
        yaw: number;
        pitch: number;
        distance: WorldUnits;
        target: [WorldUnits, WorldUnits, WorldUnits];
        eye: [WorldUnits, WorldUnits, WorldUnits];
    };
    /** Looks at `at` from `from`; without `from`, keeps the way it faces and how far back it is. */
    function look(at: MapPoint, from?: MapPoint): void;
    /** The water on the camera's lens: its drops and its sheets of water. */
    function lens(): { drops: number; sheets: number };
    /** Riding along: in a car of a ride with a track (its place in rides.list() and the car's), on a flat ride (the
     *  seat or its fixed camera), or going with a person (their place in people.list(); ride is -1). Once it is seen
     *  from (a frame or so after it starts, which no field tells), the rider's eyes in world units and the way they
     *  look. Undefined while the view is overhead. */
    function riding():
        | ({
              turn: [number, number];
              eye?: [WorldUnits, WorldUnits, WorldUnits];
              ahead?: [WorldUnits, WorldUnits, WorldUnits];
          } & (
              | { kind: "nose" | "front" | "rear"; ride: RideIndex; car: number }
              | { kind: "seat" | "fixed"; ride: RideIndex; car: number }
              | { kind: "guest" | "behind"; ride: -1; car: PersonIndex }
          ))
        | undefined;
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
    /** Who has it: the park, nobody, on sale (land or building rights only), or held back and not for sale. */
    ownership: "owned" | "owned (rights)" | "unowned" | "for sale" | "for sale (rights)" | "locked";
    /** The animal enclosure it is in, by number; -1 when in none. */
    enclosure: number;
    /** The surface height of the water on it, when it holds any. */
    water?: number;
}

/** A path tile: raised ones have a height, queue tiles on a ride's line its id. */
type PathPiece = {
    col: Tiles;
    row: Tiles;
    /** The path style it is laid in, by the name the install gives it (an open set). */
    style: string;
} & (
    | {
          /** Off the ground, on supports. */
          raised: true;
          /** In the game's height steps. */
          height: number;
      }
    | { raised: false }
) &
    (
        | {
              queue: true;
              /** The line's id, while the queue is on a ride's line. */
              line?: number;
          }
        | { queue: false }
    );

interface Lake {
    level: number;
    /** Half-tiles it covers. */
    triangles: number;
}

declare namespace map {
    function size(): { width: number; height: number };
    /** Undefined off the map. */
    function tile(col: Tiles, row: Tiles): Tile | undefined;
    /** The middle of a tile on the view (at `height` when given, else on a pool square at its surface), or with
     *  fractions that point of it; undefined when it is not in sight. */
    function screen(col: Tiles, row: Tiles, height?: WorldUnits): { x: number; y: number } | undefined;
    function paths(): PathPiece[];
    /** The tiles the rides and stalls pave: their doors, floors and the ground under stalls, and the path style
     *  each is paved in (empty where a path covers it). */
    function pavings(): { col: Tiles; row: Tiles; height: number; style: string }[];
    /** The tiles with support columns standing on them, and how many pieces each has. */
    function supports(): { col: Tiles; row: Tiles; pieces: number }[];
    /** The placed objects that are shown: scenery, rides' parts, supports, fences and the rest, with the whole-unit
     * height stored for each (null when unset), and the quarter turns of its model. */
    function objects(): {
        symbol: string;
        overlay: string;
        col: Tiles;
        row: Tiles;
        height: number | null;
        turn: number;
    }[];
    /** The pieces the pools' squares place now, their extras and changing rooms, shaped as `objects` gives them: what
     * the pool tool's work should leave shown. */
    function poolPieces(): { symbol: string; overlay: string; col: Tiles; row: Tiles; height: number | null }[];
    /** The pools' squares as laid: kind, surface height, the side a slope's, steps' or staircase's foot or a high
     * board faces, and a staircase's or board's height over the surface; and the ramps among their extras, each by
     * its square, the side it climbs to and how many units; and the tiles of their changing rooms. Sides count as the pool tool's: 0 next row, 1 next
     * column, 2 row before, 3 column before. */
    function pools(): {
        squares: {
            col: Tiles;
            row: Tiles;
            kind: "water" | "tile" | "spa" | "high dive" | "stairs" | "path" | "slope" | "steps";
            surface: number;
            side: number;
            board: number;
        }[];
        ramps: { col: Tiles; row: Tiles; side: number; step: number }[];
        rooms: { col: Tiles; row: Tiles }[];
    };
    /** The particle effects shown now, those items keep running and those fired once: the effect's name, lower
     * case, where it is as a map point, and its particles alive. */
    function effects(): { effect: string; col: Tiles; row: Tiles; up: WorldUnits; live: number }[];
    /** The rings spreading on the pools' water where people fell in: their middle as a map point, their water's
     * world height, seconds since they started (below 0 before) and their leading crest's height now. */
    function rings(): { col: Tiles; row: Tiles; up: WorldUnits; age: Seconds; crest: number }[];
    function water(): Lake[];
    /** The waterfalls: the middle of the edge each pours over and where its water lands, in tiles, the levels of
     *  the lakes it joins and how wide it is in world units. */
    function waterfalls(): {
        col: Tiles;
        row: Tiles;
        landCol: Tiles;
        landRow: Tiles;
        top: number;
        bottom: number;
        width: number;
    }[];
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
    price: Money;
    /** Whether it has an entrance and an exit to place. */
    doors: boolean;
    /** Whether it stands only on water. */
    water: boolean;
    /** The fence it takes when placed, its theme's; empty when none is named. */
    fence: string;
}

interface FacilityItem {
    overlay: string;
    symbol: string;
    /** String key of its name. */
    name: string;
    width: number;
    length: number;
    /** Hundredths of the currency. */
    price: Money;
    /** What it does for guests; "other" for those the park does not run (information). */
    kind: "toilet" | "cash machine" | "first aid" | "other";
}

interface StallItem {
    overlay: string;
    symbol: string;
    /** String key of its name. */
    name: string;
    width: number;
    length: number;
    /** Hundredths of the currency. */
    price: Money;
    /** What a new one sells: each carried item's name and the price it starts at, in hundredths. */
    goods: { item: string; price: Money }[];
}

declare namespace build {
    /** Whether the park lets an item of this symbol be built; undefined while the lists are still being read. */
    function offers(symbol: string): boolean | undefined;
    /** The install's rides placed whole; undefined while the lists are still being read. */
    function flatRides(): FlatRideItem[] | undefined;
    /** The install's stalls; undefined while the lists are still being read. Picked as rides are. */
    function stalls(): StallItem[] | undefined;
    /** The install's facilities (toilets, cash machines, first aid, information); undefined while the lists are
     * still being read. Picked as rides are. */
    function facilities(): FacilityItem[] | undefined;
    /** Starts placing a ride; false when there is no such ride. A ride built piece by piece opens its
     * construction panel on the next frame, for a custom track; with "designs" it also opens the window of its
     * kind's saved designs ("design_browser"), as picking it in its build list does when it has some. An
     * enclosure fence, or the enclosure list's delete tool ("", "EnclosureDelete"), takes out the enclosure tool. */
    function pick(overlay: string, symbol: string, offer?: "designs"): boolean;
    /** The construction panel's ride (its place in rides.list()), how many pieces fit where the track goes on,
     * the one picked, whether it can be built there (and its arrows show), what laying it (or the design) where
     * its ghost shows costs as the panel shows it, in hundredths, whether it may not stand there (for a design,
     * any of its pieces or doors) and whether for land the park does not own, for a design in hand the string key of why
     * it may not stand ("" when it may) and the tiles where its ghost meets a pool beside them (each with the height and the side, as
     * the pools' sides count, the pool square must be across), the quarter turns of the first piece, the doors a design's ghost shows, and the files of the saved designs of its kind (the
     * player's own, then the install's), the piece clicked mid-track to build over (its place in the ride's
     * track), null while building at the open end, and on a ride of two tracks side by side the split button's
     * state: 1 joined from the left track, 2 the left alone, 3 joined from the right, 4 the right alone; 0 on
     * other rides. Undefined while the panel is shut. */
    function construction():
        | {
              ride: RideIndex;
              offered: number;
              piece: string | null;
              ghost: boolean;
              arrows: boolean;
              ghostAt: { col: Tiles; row: Tiles; height: number } | null;
              cost: Money;
              clashes: boolean;
              unowned: boolean;
              refusal:
                  | ""
                  | "PlaceObject_NotOwned"
                  | "PlaceObject_TooHigh"
                  | "PlaceObject_Intersects"
                  | "PlaceObject_IntersectsGround"
                  | "PlaceObject_MustEndOnPoolEdge";
              turn: number;
              poolSides: { col: Tiles; row: Tiles; height: number; side: number }[];
              ghostDoors: number;
              ghostPlatform: string;
              completing: boolean;
              proposed: number;
              complaint:
                  "" | "RideAutoCompleteNotEnoughCash" | "RideAutoCompleteNoSolution" | "RideAutoCompleteOutOfTime";
              testing: boolean;
              testAt: number | null;
              testWrecks: number;
              designs: string[];
              /** How many of the designs its window last listed as locked, waiting for a career scenario. */
              locked: number;
              /** The file of the design whose details the designs window shows, or null. */
              shown: string | null;
              selected: number | null;
              split: 0 | 1 | 2 | 3 | 4;
          }
        | undefined;
    /** What building gave to be heard: how many thumps of something put down were asked for since the park opened
     * (several close together are heard as one), and while a shaping stroke goes the loudness of its rumble, 0..1. */
    function sounds(): { thumps: number; rumble: number | null };
    /** With the pool tool out: the piece its ghost shows (an extra, a changing room or the paths page's next piece)
     * and where, and whether the arrow over the next pool path piece shows; undefined while the tool is away. */
    function pooling():
        | ({ arrow: boolean } & (
              { ghost: string; ghostAt: { col: Tiles; row: Tiles; height: number } } | { ghost: null }
          ))
        | undefined;
    /** The install's saved designs, by file, with the symbol and overlay that build.pick() takes for their kind
     * (overlay null when the install has no ride of that kind); undefined while the lists are still being read. */
    function designs(): { file: string; symbol: string; overlay: string | null }[] | undefined;
    /** Puts the placing tool, the enclosure tool or the pool tool away, as Escape does. */
    function stop(): void;
    /** While an enclosure drag is held: what letting go would cost in hundredths (below 0 when it pays back),
     * and how many tiles it would take in or out; undefined otherwise. */
    function enclosing(): { cost: Money; tiles: number } | undefined;
    /** What is being placed, what keeps it from where the pointer is, whether a stall's facing arrow shows, and
     * how the ground under a ride or stall is levelled and what that costs, in hundredths; undefined while nothing
     * is. */
    function placing():
        | {
              step: "ride" | "entrance" | "exit" | "scenery";
              problem:
                  | "none"
                  | "not owned"
                  | "path"
                  | "blocked"
                  | "off water"
                  | "under water"
                  | "off path"
                  | "off enclosure"
                  | "into ground";
              affordable: boolean;
              flatten: "off" | "sharp" | "smooth";
              levelling: Money;
              ghost: boolean;
              arrow: boolean;
              site?: { col: Tiles; row: Tiles; turns: number; height: number };
          }
        | undefined;
}

/** A place a ride's part stands: its lowest tile, and the quarter turn it faces. */
interface Spot {
    col: Tiles;
    row: Tiles;
    direction: number;
}

/** What rides.list() gives for a ride the simulation runs (`joined`). Which of these it gives depends on what the
 *  simulation runs for the ride, which no field names, so they stay optional: each group is given together, and
 *  a facility has only `customers` and `servedFrom`. A ride not joined has none of them. */
interface RideRun {
    customers?: number;
    /** A facility: the path tile it serves from. */
    servedFrom?: { col: Tiles; row: Tiles };
    /** How many of the simulation's attractions are this ride (1 unless taken on twice). */
    simulated?: number;
    /** On a track: the trains it runs, and how many are under way now. */
    running?: number;
    moving?: number;
    /** On a track: each train's front along the track (metres), its speed along it (m/s), a slide car's
     *  swing up its trough's wall (radians, to its right; null for other trains), its length, and the
     *  station it stands at (0 the first, k the k-th stop after it), null while under way; the station it
     *  is held up in behind the train ahead, short of its end, numbered alike, or null; and the riders
     *  still in its seats. On a lift's car, how far it has come up the lift and how far its front is along
     *  the car's track (metres); else null. */
    trainsAt?: {
        front: number;
        speed: number;
        lean: number | null;
        length: number;
        station: number | null;
        held: number | null;
        riders: number;
        lift: {
            phase: "Boarding" | "Engaging" | "Rising" | "AtTop" | "Releasing" | "Leaving";
            aboard: number;
        } | null;
    }[];
    /** On a track or a lazy river: the length of its course, metres. */
    trackLength?: number;
    /** A lazy river: its stations, how many of them a pool complex joins, and the middle of the paving,
     *  pool path or stairs square beside each joined one, where swimmers get on and off. */
    riverStations?: number;
    riverJoined?: number;
    riverSquares?: { col: Tiles; row: Tiles; up: WorldUnits }[];
    /** A slide that swimmers climb on foot, as an inflatable ship: its ways up and down, and the foot of
     *  its steps, where swimmers get on. */
    climbWays?: number;
    climbFoot?: { col: Tiles; row: Tiles; up: WorldUnits };
    /** Hundredths of the currency. */
    price?: Money;
    open?: boolean;
    /** Broken down, waiting for a mechanic. */
    broken?: boolean;
    /** Whether its entrance was found on the paths. */
    onPaths?: boolean;
    /** Where the guests walking to it are now. */
    heading?: { col: Tiles; row: Tiles }[];
    capacity?: number;
    cycleSeconds?: Seconds;
    queue?: number;
    queueLine?: number;
    /** Rides with several stations: the stations after the first its trains stop at for riders. */
    stops?: number;
    /** The path tile its first station lets riders out onto; absent while no path joins its exit. */
    exitAt?: { col: Tiles; row: Tiles };
    riders?: number;
    /** Excitement, intensity and nausea, 0..1. */
    ratings?: [number, number, number];
    /** Rides whose trains have crashed: how often, and where the wrecks of the crashed trains are now. */
    crashes?: number;
    wrecks?: (MapPoint & { state: "flying" | "burning" | "gone" })[];
}

/** A ride, as rides.list() gives it: its kind says whether it has a track, and `joined` whether the simulation
 *  runs it. */
type RideInfo = {
    index: RideIndex;
    name: string;
    /** The fence round it, without its tag; empty when it has none. */
    fence: string;
    /** With its tag, as "Twist:anr". */
    symbol: string;
    overlay: string;
    /** A flat ride, as `kind` "flat" says too. */
    flat: boolean;
    site: Spot | null;
    /** Its first station's doors; `stations` has every station's. */
    entrance: Spot | null;
    exit: Spot | null;
    stations: { entrance: Spot | null; exit: Spot | null }[];
    /** Whether it was pulled down; it keeps its place in the list. */
    demolished: boolean;
    /** Samples in its graphs: its last lap's, a tenth of a second each. */
    graphSamples: number;
} & (
    | {
          /** A ride built piece by piece, with trains on its track. */
          kind: "tracked";
          /** How many pieces it has. */
          pieces: number;
          /** How many trains it has, and cars in each. */
          trains: number;
          carsPerTrain: number;
          /** Passenger cars in each train, without the links between them, when the park file says. */
          passengerCars?: number;
          /** Each piece's name, in track order. */
          track: string[];
          /** Each piece's three colours (palette places), in track order. */
          trackColours: number[][];
          /** The platform pieces of its stations, in track order. */
          platforms: string[];
          /** The three colours of each of `platforms`. */
          platformColours: number[][];
          /** The squares its stations' platforms stand on, the height of their decks and the quarter turn of their
           *  pieces, in track order. */
          platformSquares: { col: Tiles; row: Tiles; height: number; turn: number }[];
          /** Each piece's arm angles in degrees, base first, or null where it holds none. */
          arms: (number[] | null)[];
          /** Where each of its cars stands now, and the way it faces (radians round the vertical, 0 along +row).
           *  A rowing boat out on the water: the oars it rows with, both or those turning it to its left or right.
           *  A lift's car is marked `lift`. */
          cars: (MapPoint & { facing: number; stroke?: "both" | "left" | "right"; lift?: true })[];
      }
    | {
          /** A ride placed whole, or a facility (toilets, cash machines, first aid, information). */
          kind: "flat" | "facility";
      }
) &
    (({ joined: false } & { [K in keyof RideRun]?: undefined }) | ({ joined: true } & RideRun));

/** Why a ride stays as it was, as rides.setState() gives it: a string key. */
type RideRefusal =
    | "TrackedRideIsDisjoint"
    | "RideHasNoStation"
    | "CannotOpenOrTestInsufficientPieces"
    | "CannotOpenOrTestNotCircuit"
    | "CannotTestTrackedRide"
    | "CannotOpenTrackedRideWhileEditing"
    | "CannotOpenOrTestNoReverseCableLiftHill"
    | `CantOpen${"Ride" | "PoolRide"}${"Missing" | "No"}${"Entrance" | "Exit"}`
    | "CannotOpenExitBeforeEntrance";

declare namespace rides {
    function list(): RideInfo[];
    /** Puts ride `i` of list() in `state`, as its window's open-or-close tab does. Null once changed, else the
     *  string key of why the ride stays as it was (a track without a station, a missing exit...). */
    function setState(i: RideIndex, state: "closed" | "testing" | "open"): RideRefusal | null;
    /** Has every train of ride `i` (its place in list()) run at `speed` m/s all the way round, as a ride set to a
     *  fixed speed does; 0 or less lets them run free again. Throws for a ride the park runs no trains on. */
    function setFixedSpeed(i: RideIndex, speed: number): void;
    /** Opens ride `i`'s window, as clicking the ride does, on the next frame. */
    function show(i: RideIndex): void;
    /** The ride the info panel shows and its tab, by the tab's help key; undefined when it shows no ride. */
    function shown(): { ride: RideIndex; tab?: string } | undefined;
}

type StallInfo = {
    index: StallIndex;
    name: string;
    /** With its tag, as "BurgerStall:sta". */
    symbol: string;
    overlay: string;
    site: Spot | null;
    open: boolean;
    /** What it sells, in its menu's order: each item's symbol with its tag, as "Burger:cid", its price in
     *  hundredths, its name as kept ("%ME%" for the item's own) and which of its extras go on, a bit each. */
    items: { item: string; price: Money; name: string; extras: number }[];
    /** How much of each extra it puts on: 0 none up to 4 loads, 5 as the guest picks. */
    extras: { extra: string; level: number }[];
    /** Whether changes to its menu go to every stall of its kind. */
    sync: boolean;
    /** Whether the info panel shows it. */
    shown: boolean;
    /** Whether it was pulled down; it keeps its place in the list. */
    demolished: boolean;
    /** Whether its building moves: stalls whose models have a motion play it round and round, open or not. */
    moving: boolean;
} & (
    | {
          /** The simulation runs it, once a path runs past the tile it faces. */
          joined: true;
          /** The price it sells at, in hundredths, and whether it is open to guests there. */
          price: Money;
          selling: boolean;
          /** How many guests have bought from it. */
          customers: number;
          /** Broken down, shut until a mechanic mends it. */
          broken: boolean;
      }
    | { joined: false }
);

declare namespace stalls {
    function list(): StallInfo[];
    /** Breaks stall `stall` of list() down at once, as wear would; whether it was run by the park and whole. */
    function breakDown(stall: StallIndex): boolean;
    /** Opens stall `i` of list() in the info panel, as a click on it does. */
    function show(i: StallIndex): void;
    /** Puts stall `i` of list() in `state`, as its window's open-or-close tab does. Null once changed, else the
     *  string key of why it stays as it was (nothing on its menu). */
    function setState(i: StallIndex, state: "closed" | "open"): "CantOpenStallNoItems" | null;
    /** Sets the price of item `item` on stall `i`'s menu, in hundredths, as its window's items page does. */
    function setPrice(i: StallIndex, item: number, hundredths: Money): void;
}

/** What a person is at, as people.list() gives it for those the simulation walks. */
type PersonState =
    | "walking"
    | "queueing"
    | "riding"
    | "waiting"
    | "sitting"
    | "lost"
    | "buying"
    | "swimming"
    | "held"
    | "dropped"
    | "thrown"
    | "sliding"
    | "still";

/** A person the simulation walks: where they are (tiles, and world height `up`) and what they are at, `state`, with
 *  what goes with it. The other fields do not follow from the state (a swimmer's fields hold in the water, on a
 *  river and held over the pool alike), so they stay optional. */
type PersonOnPaths = {
    col: Tiles;
    row: Tiles;
    up: WorldUnits;
    /** Their party's number, shared by its members, while they are with it. */
    party?: number;
    /** On their way out of the park. */
    leaving?: true;
    /** Walking in from a spawn point, not through the gate yet. */
    arriving?: true;
    /** How they go on foot. */
    gait?: "walk" | "run" | "weary" | "queasy" | "bursting";
    /** Food or drink from a stall still being had, or its empty. */
    carrying?: "food" | "drink" | "empty";
    /** A gesture they make now, by the motion it shows. */
    gesture?:
        | "jumpjoy"
        | "laugh"
        | "nodhead"
        | "shakehead"
        | "dizzy"
        | "vomit"
        | "knockdown"
        | "handticket"
        | "applause"
        | "lookwatch"
        | "disapproval"
        | "cry"
        | "horror"
        | "lookmap";
    /** Looking at something as they walk on past it. */
    glancing?: true;
    /** Stopped at a path item. */
    lingering?: PathItemKind;
    /** In a pool complex, or held or falling out of one. */
    swimwear?: true;
    /** A swimmer: in the pool's water, not on the paving and coping round it. */
    in_water?: boolean;
    /** A swimmer crossing the water's edge, how. */
    pool_edge?: "dive in" | "climb out" | "ladder out" | "ladder in";
    /** A swimmer on a pool's sun lounger, under its shower or at another of its extras. */
    pool_extra?: "lounger" | "shower" | "other";
    /** Stopped round someone: an entertainer's show, a VIP for an autograph, or walking after the VIP. */
    watching?: "show" | "autograph" | "chasing";
    /** An entertainer in a show, the move they make now. */
    show?:
        | "wave"
        | "idle1"
        | "idle2"
        | "dancelevel1"
        | "dancelevel2"
        | "dancelevel3"
        | "falloverin"
        | "falloverout"
        | "dejection";
} & (
    | {
          /** A swimmer off a diving board, how far, and whether it is a high one. */
          diving: "climb" | "walk" | "jump" | "fall" | "surface";
          high_board: boolean;
      }
    | { diving?: undefined; high_board?: undefined }
) &
    (
        | {
              state: "sitting";
              /** The id of the bench's object, which of its seats, and whether they are getting up. */
              bench: PlacedObjectId;
              seat: number;
              rising: boolean;
          }
        | {
              state: "riding";
              /** The ride's place in rides.list, once found. */
              ride?: RideIndex;
              /** Seated on a ride, how they react to it. */
              reaction?: "waiting" | "calm" | "hands up" | "covering eyes" | "strained";
              /** Round a lazy river, resting in their ring or paddling. */
              floating?: "resting" | "paddling";
              /** On a slide climbed on foot, up its steps or down the slide. */
              climbing?: "climbing" | "sliding";
          }
        | {
              state: "sliding";
              /** Down ground the land tools tipped too steep, the motion they slide in. */
              slide: "frontslide" | "backslide" | "sitslide" | "dangleangry";
          }
        | { state: Exclude<PersonState, "sitting" | "riding" | "sliding"> }
    );

/** A guest's passport, once stamped: each stamp's post (as pathItems()' object), ink (0xAARRGGBB) and place on the
 *  page (0 to 1 across and down), and whether they were given the park's gift. */
interface Passport {
    stamps: { post: PlacedObjectId; colour: number; x: number; y: number }[];
    gift: boolean;
}

/** A person shown in the park, as people.list() gives them: guest or staff, and, when the simulation walks them,
 *  where they are and what they are at (no `state` when it does not). */
type Person = {
    index: PersonIndex;
    name: string;
    /** The point of the map they stand on, in tiles. */
    at: { col: Tiles; row: Tiles } | null;
    /** The numbers of the thoughts their window lists. */
    thoughts: number[];
} & (
    | {
          staff: false;
          /** What their window and the guest list show, 0 to 1. */
          happiness: number;
          passport?: Passport;
      }
    | ({
          staff: true;
          /** A seller: the id of the object of the stall or ride entrance they serve at. */
          post?: PlacedObjectId;
      } & (
          | {
                /** A worker the simulation runs: their job, and what their time at work goes on now. */
                role: "handyman" | "mechanic" | "guard" | "other" | "vendor" | "keeper" | "inspector" | "entertainer";
                job: "none" | "sweep" | "emptybin" | "mend" | "water" | "fix" | "inspect" | "watch" | "rest";
            }
          | { role?: undefined; job?: undefined }
      ))
) &
    ({ state?: undefined } | PersonOnPaths);
/** A person the simulation walks, with where they are and their `state`. */
type WalkedPerson = Exclude<Person, { state?: undefined }>;

declare namespace people {
    /** The people shown in the park, guests and staff, in the order the park keeps them. */
    function list(): Person[];
    /** Opens the window of person `i` of list() on the next frame, as a click on them does. */
    function show(i: PersonIndex): void;
    /** Renames person `i` of list() on the next frame, as typing in their window's name box does. */
    function rename(i: PersonIndex, name: string): void;
}

/** A level of a guest's feelings and needs, as the developer windows' editor names them. */
type GuestLevel =
    "happiness" | "energy" | "nausea" | "wet" | "heat" | "hunger" | "thirst" | "bladder" | "swim" | "tolerance";

declare namespace editor {
    /** Whether the developer windows' editor (F12) was drawn on the last frame. */
    function shown(): boolean;
    /** Picks person `i` of people.list(), a guest the simulation walks, as a click on their row in the editor's
     *  guest list does. */
    function pick(i: PersonIndex): void;
    /** The picked guest as the simulation has them: their levels, 0 to 1, and those held. */
    function guest(): { name: string; levels: Partial<Record<GuestLevel, number>>; held: GuestLevel[] } | undefined;
    /** Sets the picked guest's `level`, as its slider does; false when they have none. */
    function set(level: GuestLevel, value: number): boolean;
    /** Holds the picked guest's `level` where it is, or lets it go, as its hold switch does. */
    function hold(level: GuestLevel, on: boolean): void;
}

declare namespace structures {
    /** The structure being placed, while the placing tool is out: what its ghost shows and whether it may go there. */
    function placing():
        | {
              name: string;
              items: number;
              problem: "none" | "not owned" | "blocked" | "path";
              affordable: boolean;
              cost: Money;
              turns: number;
              height: number;
              ghost: boolean;
              at?: { col: Tiles; row: Tiles };
          }
        | undefined;
    /** How many tiles the picking out tool has marked. */
    function marked(): number;
}

declare namespace animals {
    /** The park's animals, each with a number of its own, its species, the enclosure they live in (-1 for none), whether they are in a
     *  crate or out of every enclosure, their health from 0 to 1, and the point of the map they stand on, in
     *  tiles. */
    function list(): {
        id: number;
        species: string;
        enclosure: number;
        crated: boolean;
        escaped: boolean;
        health: number;
        at: { col: Tiles; row: Tiles };
    }[];
}
