// SPDX-License-Identifier: GPL-3.0-or-later
//
// In-memory model of the rides, tracks, trains, cars, shops and stalls in a park save (or a coaster design,
// which uses the same container), plus a loader that builds it from a decoded save.
//
// Conventions:
//  - Object references are u64 ids; 0 means none. A reference whose id is not in the file's object table is
//    stale and is loaded as 0, never as an error.
//  - Money is an integer count of hundredths of the currency unit (a stored 300 means 3.00).
//  - Distances are metres, times seconds, speeds metres per second. Test values for forces are stored as
//    accelerations in metres per second squared (divide by 9.81 for a value in g).
//  - Enumerations keep unknown stored values: their underlying type is a plain integer, so a value outside
//    the named set is still representable and is never rejected.
//  - Attributes that a file generation does not store keep the defaults given below.
//  - Fields whose bytes are uninitialised memory in real files are masked or not exposed.
// Only fields whose meaning is established are exposed; anything else is left out rather than guessed.
#pragma once

#include "core/id.hpp"
#include "core/types.hpp"
#include "save/save_file.hpp"
#include "save/value.hpp"
#include "units/units.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace rct3::save::model
{

    /// An object id from the save. 0 is none.
    using ObjectId = u64;

    // ---------------------------------------------------------------------------------------------------------
    // Enumerations (named values are the known ones; any other integer may appear and must be tolerated)

    enum class AttractionState : i32
    {
        Closed = 0,
        Testing = 1,
        Open = 2
    };

    enum class OperatingMode : i32
    {
        ContinuousCircuit = 0,
        PoweredLaunch = 1,
        ReverseInclinedLaunch = 2,
        Boat = 3,
        Race = 4,
        Shuttle = 5,
        UpwardsLaunch = 6,
        FreefallDrop = 7,
        UpAndDown = 8,
        MiniGolf = 9,
        Slide = 10,
        ReverseCableLiftHill = 11,
        WaterTransport = 12,
        Aquarium = 13,
        MultiplePoweredLaunch = 14,
        LazyRiver = 15,
    };

    enum class BreakdownKind : i32
    {
        None = 0,
        EmergencyStop = 1,
        RestraintsFailToOpen = 2,
        RestraintsFailToClose = 3,
        DoorsFailToOpen = 4,
        DoorsFailToClose = 5,
        TrainStuckLeavingStation = 6,
        StationBrakesFail = 7,
        ControlFailure = 8,
    };

    enum class RepairStatus : i32
    {
        Fine = 0,
        AwaitingBreakdown = 1,
        MechanicCalled = 2,
        MechanicOnTheWay = 3,
        BeingFixed = 4,
    };

    /// What kind of attraction object a Ride was loaded from.
    enum class RideKind : u8
    {
        Tracked, ///< Coaster or other track-guided ride with trains
        Flat,    ///< Animated (flat) ride: no track
        Special, ///< Special attraction: toilets, cash machines and similar facilities
    };

    // ---------------------------------------------------------------------------------------------------------
    // Shared parts

    /// Three colour slot indices, each 0..31 in valid files (stored values are kept).
    using RideColours = std::array<u8, 3>;

    /// Where a placement record puts the object it holds: a tile, a height and a quarter turn.
    struct PlacementSpot
    {
        ObjectId object = 0; ///< The placed scenery object, 0 if none
        i32 col = 0;
        i32 row = 0;
        i32 height = 0;   ///< Whole units, as placed objects store it
        u8 direction = 0; ///< Quarter turns, 0..3
    };

    /// Counters and history that every attraction (ride or stall) carries.
    struct AttractionCommon
    {
        ObjectId id = 0;
        ObjectId kind_asset = 0;      ///< Reference to the asset-database entry of the attraction's kind
        std::string symbol { };       ///< Asset symbol of the kind, e.g. "<name>:<tag>"; compare case-insensitively
        std::string overlay_path { }; ///< Asset overlay path (no extension)
        std::string name { };         ///< User name, a text key, or a numbered placeholder
        AttractionState state = AttractionState::Closed;
        u32 flags = 0; ///< Bit set; all bits preserved. 0x40 never opened, 0x200 low-reliability warning
        /// Set while the attraction is broken down. The repair status can stay at a mechanic called after the
        /// repair, so this bit is what tells. Found by saving one park with three rides as each broke down in
        /// turn: all three kept the same repair status, while this bit was set on exactly the broken ones, which
        /// were also the ones taped off and the ones missing from the park's ideal guest count.
        static constexpr u32 kBrokenDown = 1u << 2;
        units::Seconds build_time { }; ///< Game clock when built
        i64 total_guests = 0;
        units::Money total_profit { };
        units::Money running_cost { }; ///< Assessed running cost per interval
        f32 popularity = 0;
        f32 satisfaction = 0;
        RepairStatus repair_status = RepairStatus::Fine;
        bool mechanic_asked = false;                             ///< rct3++'s own saves: the player called a mechanic to inspect it, not yet come
        std::optional<units::Seconds> time_since_inspection { }; ///< None when never inspected
        std::optional<units::Seconds> time_since_breakdown { };  ///< None when it never broke down
        std::optional<units::Seconds> time_since_crash { };      ///< None when it never crashed
        ObjectId placement = 0;                                  ///< World placement record (stalls, flat rides, facilities), 0 if none
        std::optional<PlacementSpot> spot { };                   ///< Where that record puts its object; absent without one
        u8 facing = 0;                                           ///< Quarter turns, 0..3
        /// Pulled down this session; never stored. It keeps its place so lists by attraction index stay put,
        /// and saving leaves it out.
        bool demolished = false;
        static constexpr u32 kNeverOpened = 1u << 6;
    };

    /// One of the ten statistics slots. Stored as a ring whose head is not identified, so file order is kept.
    struct IntervalStats
    {
        i64 guests = 0;
        units::Money income { };
        units::Money losses { };
    };

    // ---------------------------------------------------------------------------------------------------------
    // Ride statistics and ratings

    /// Results of one measured lap. All force values are accelerations in m/s^2.
    struct TestResults
    {
        f32 max_speed = 0;
        f32 average_speed = 0;
        units::Seconds ride_time { };
        units::Distance ride_length { };
        f32 max_positive_vertical = 0;
        f32 max_negative_vertical = 0;
        f32 max_lateral = 0;
        units::Seconds air_time { };
        i32 drop_count = 0;
        units::Distance highest_drop { };
        i32 inversion_count = 0;
        i32 lift_hill_count = 0;
        /// Corner counts by turned angle: index 0..3 = 90, 180, 270, 360 degrees.
        std::array<i32, 4> corners_flat { };
        std::array<i32, 4> corners_banked { };
        std::array<i32, 4> corners_sloped { };
        /// Special section counts: water splash, rapids, spinning tunnel, waterfall, turntable, whirlpool, helices.
        std::array<i32, 7> special_sections { };
        bool valid = false; ///< Only bit 0 of the stored word is meaningful; the rest is uninitialised memory
    };

    struct Assessment
    {
        bool valid = false; ///< When false the other fields are not meaningful and are zero
        f32 excitement = 0;
        f32 intensity = 0;
        f32 nausea = 0;
        f32 reliability_base = 0;
        units::Share under_cover { };
        f32 value = 0; ///< Score
    };

    struct RideStats
    {
        std::optional<TestResults> current_lap { }; ///< Accumulators of the lap in progress
        std::optional<TestResults> last_lap { };    ///< Last completed valid lap
        std::optional<TestResults> combined { };    ///< Multi-station result, newer files only
    };

    /// Samples a second in a ride's graphs.
    inline constexpr usize kGraphSamplesPerSecond = 10;

    /// Per-ride graphs of the last lap run, newest sample first (at most 200 in a save): speed in m/s, altitude in
    /// metres, forces in m/s^2.
    struct RideGraphs
    {
        std::vector<f32> speed { }, acceleration { }, altitude { }, vertical_g { }, lateral_g { };
    };

    struct Breakdown
    {
        BreakdownKind current = BreakdownKind::None;
        BreakdownKind last = BreakdownKind::None;
        std::optional<i32> station { };
        std::optional<i32> train { };
        std::optional<i32> car { };
        std::optional<i32> seat { };
        i32 inspection_interval = 0;
        f32 reliability = 100; ///< Percent 0..100
        /// Eight window counters, each 0..20.
        std::array<u8, 8> downtime_windows { };
        /// The downtime percentage as saved. It is worked out just before the windows move on by one, so it can
        /// still include a window the counters no longer hold.
        std::optional<f32> saved_downtime_percent { };
        /// The saved percentage, or when the save has none, one derived from the windows: sum * 100 / 160.
        [[nodiscard]] f32 downtime_percent() const noexcept;
    };

    struct OperatingOptions
    {
        bool block_sectioned = false;
        bool sync_trains = false;
        bool leave_if_other_train_enters_station = false;
        std::optional<i32> min_wait { }; ///< Whole seconds
        std::optional<i32> max_wait { }; ///< Whole seconds
        i32 wait_for_load = 4;           ///< Load a train waits for: 0 full, 1 three quarters, 2 half, 3 quarter, 4 any
        units::Pace launch_speed { };
        units::Pace lift_speed { };
        units::Pace brake_speed { };
        units::Pace block_brake_off_speed { };
        std::optional<units::Pace> fixed_speed { };
        i32 circuits = 1; ///< Times round the track, or through the station and back for a shuttle
    };

    struct RaceState
    {
        bool in_progress = false;
        bool over = false;
        std::optional<i32> winner { }; ///< Train index
        f32 seconds_since_end = 0;
    };

    /// A guest's recent opinion of the attraction.
    struct ThoughtEntry
    {
        ObjectId guest = 0;
        i32 thought = 0; ///< Index into the game's thought table (meaning not decoded here)
        f32 time = 0;
        i64 variant = 0;
    };

    // ---------------------------------------------------------------------------------------------------------
    // Stations and queues

    struct Station
    {
        ObjectId entrance = 0; ///< Placement record; 0 when not built
        ObjectId exit = 0;
        std::optional<PlacementSpot> entrance_spot { }; ///< Where those records put the entrance and exit
        std::optional<PlacementSpot> exit_spot { };
        f32 entrance_timer = 0;
        f32 exit_timer = 0;
        ObjectId queue_line = 0;       ///< 0 when the line does not exist
        i32 queue_size = 0;            ///< Stored size of the queue line (0 without one)
        i32 queue_tiles = 0;           ///< Queue path tiles in the file that belong to the line
        units::Seconds queue_time { }; ///< Estimated queue time
    };

    // ---------------------------------------------------------------------------------------------------------
    // Track

    /// Platform part of a station piece.
    struct TrackPlatform
    {
        ObjectId id = 0;
        i32 tile_x = 0, tile_z = 0;
        i32 height = 0;
        u8 direction = 0;
        u8 corner = 0;
        u32 flags = 0;
        RideColours colours { };
    };

    struct TrackPiece
    {
        ObjectId id = 0;
        std::string symbol { }; ///< Track section asset symbol: identifies the piece shape
        ObjectId asset = 0;     ///< Asset-database entry of the piece
        ObjectId scenery = 0;   ///< World object that carries the piece, 0 if none
        i32 tile_x = 0;
        i32 tile_z = 0;
        i32 height = 0;                     ///< Integer height level
        f32 height_adjust = 0;              ///< Newer files only
        u8 direction = 0;                   ///< Quarter turns, 0..3
        u8 corner = 0;                      ///< Sub-tile corner, 0..3
        bool reversed = false;              ///< Traversed against the section's authored direction
        units::Distance start_distance { }; ///< From the start of the track, increasing along the chain
        units::Distance reverse_start_distance { };
        RideColours colours { };
        /// Per-piece operating parameters. Stored as plain integers; their meaning is only partly established.
        std::optional<i32> brake_speed { };
        i32 holding_time = 0;
        std::optional<i32> speed_class { };        ///< The speed the player set the piece to, in whole m/s: a booster pushes up to it
        std::optional<TrackPlatform> platform { }; ///< Station pieces
        /// Of a ride whose cars ride on arms, the arm's four joint angles in degrees where the player set them:
        /// the arm holds them at the piece's middle.
        std::optional<std::array<f32, 4>> arm_angles { };
        /// Of a ride whose cars turn as the player sets, degrees they turn to on this piece. Not read from files.
        f32 car_turn = 0;
        /// Of a ride whose cars face as the player sets: 0 forwards, 1 backwards, 2 spinning freely. Not read from
        /// files.
        u8 facing = 0;
        u32 segment = 0; ///< Index of the owning segment within Track::segments
    };

    struct TrackSegment
    {
        ObjectId id = 0;
        usize first_piece = 0; ///< Range [first_piece, first_piece + piece_count) of Track::pieces
        usize piece_count = 0;
        /// Number of pieces in the file that name this segment as owner (equals piece_count in sound files).
        usize owned_pieces = 0;
        /// True when this segment's own last piece links to its first and back (a loop of its own, as in a ride
        /// made of several parallel closed loops).
        bool closed = false;
    };

    struct Track
    {
        ObjectId id = 0;
        ObjectId ride = 0;
        u8 facing = 0;
        bool prototype = false;
        RideColours colours { };
        std::vector<TrackSegment> segments { };
        std::vector<TrackPiece> pieces { }; ///< All pieces in travel order
        /// True when the whole chain closes on itself (last piece links to the first and back) or when every
        /// segment is a closed loop of its own. Derived from the chain; the stored circuit flag is not used
        /// because real files contradict it.
        bool circuit = false;
        /// True when the chain walk ended early: a link was unresolved, repeated a piece, or ran out of segment.
        bool truncated = false;
    };

    // ---------------------------------------------------------------------------------------------------------
    // Trains and cars

    /// What a train is doing, as the number it is stored as. Only numbers whose meaning the saves show are
    /// named: tallied over 75 saves with `save-dump --trains`, by the train's speed, the piece under its front
    /// and how long it had been in the state. Other numbers are valid values of the type too.
    enum class TrainState : i32
    {
        None = 0,      ///< The save holds no state for it
        Waiting = 1,   ///< Standing still at its station, for minutes at a time (112 trains, 24 saves)
        Running = 13,  ///< Moving round its track away from the stations (294 trains, 25 saves)
        Arriving = 22, ///< Slowing to a stop on a station platform (140 trains, 18 saves)
        Parked = 68,   ///< Held for hours at a track's end with its cars on no piece (69 trains, 5 saves)
    };

    /// What a car is doing, as the number it is stored as; named as TrainState is, from the trains they are in.
    enum class CarState : i32
    {
        None = 0,     ///< The save holds no state for it
        Running = 1,  ///< In a running train, out on the track (935 cars)
        Arriving = 2, ///< In a train arriving at a station (104 cars)
        Waiting = 13, ///< Still on a station platform, in a waiting train (459 cars)
        Parked = 16,  ///< In a parked train, on no piece (69 cars)
    };

    struct Car
    {
        ObjectId id = 0;
        i32 index = 0;     ///< Position in the train as stored
        i32 car_count = 0; ///< Cars in the train as stored on the car
        /// Which of the (up to six) car designs of the train asset this car uses. Link cars use a distinct slot.
        i32 design_slot = 0;
        CarState state = CarState { }; ///< As stored; most numbers are not named
        i32 passengers = 0;
        units::Distance distance { }; ///< Along the track
        units::Pace speed { };
        units::Distance length { };
        f32 mass = 0;
        bool reversed = false;
        f32 front_wheel_distance = 0;
        f32 rear_wheel_distance = 0;
        std::array<f32, 3> position { }; ///< World position in metres (valid when position_valid)
        bool position_valid = false;
        /// How the car is turned from its model: about this axis, by its length in radians.
        std::array<f32, 3> orientation { };
        ObjectId front_piece = 0;
        ObjectId rear_piece = 0;
    };

    struct Train
    {
        ObjectId id = 0;
        ObjectId ride = 0;                 ///< Back reference as stored
        i32 index = 0;                     ///< Index as stored; equals the position in Ride::trains in sound files
        TrainState state = TrainState { }; ///< As stored; most numbers are not named
        units::Seconds state_time { };     ///< In the current state
        ObjectId front_piece = 0;
        ObjectId rear_piece = 0;
        units::Distance distance { }; ///< Along the track
        units::Pace speed { };
        f32 acceleration = 0;
        std::optional<i32> station { }; ///< Station indices
        std::optional<i32> start_station { };
        std::optional<i32> current_station { };
        i32 lap_count = 0;
        i32 shuttle_direction = 0; ///< 0 or 1
        f32 static_friction = 0;
        f32 dynamic_friction = 0;
        f32 mass = 0;
        units::Distance length { };
        std::string symbol { }; ///< Train design asset symbol
        std::string overlay_path { };
        std::vector<Car> cars { }; ///< Front to back
    };

    // ---------------------------------------------------------------------------------------------------------
    // Rides and stalls

    struct Ride
    {
        RideKind kind = RideKind::Tracked;
        AttractionCommon base { };
        OperatingMode mode = OperatingMode::ContinuousCircuit;
        units::Money ticket_price { };
        std::string music { }; ///< The file of the game's music folder it plays while open; empty for none
        std::vector<Station> stations { };
        std::vector<IntervalStats> intervals { };
        std::vector<ThoughtEntry> thoughts { };
        Assessment assessment { };
        RideStats stats { };
        RideGraphs graphs { };
        Breakdown breakdown { };
        OperatingOptions options { };
        RaceState race { };
        RideColours car_colours { }, fence_colours { }, queue_colours { };
        ObjectId fence = 0; ///< Fence asset entries, 0 if none
        ObjectId override_fence = 0;
        /// The asset symbols and overlays those entries name, as PlacedObject::symbol and overlay; empty when the
        /// entry is none or does not resolve.
        std::string fence_symbol { }, fence_overlay { };
        std::string override_fence_symbol { }, override_fence_overlay { };
        std::string platform_style { }; ///< Tracked rides
        std::string platform_type { };

        // Tracked rides
        ObjectId track_id = 0;
        std::optional<Track> track { }; ///< Absent when the reference does not resolve
        std::vector<Train> trains { };
        std::optional<i32> train_count { };    ///< Requested; none when never set
        std::optional<i32> cars_per_train { }; ///< Requested passenger cars per train; none when never set
        i32 train_selection = 0;               ///< Index of the chosen train design
        struct Seat
        {
            i32 car = 0, seat = 0, train = 0;
        };
        std::vector<Seat> seats { };
        std::vector<ObjectId> block_sections { }; ///< Occupying train per block section; 0 free
        std::vector<i32> sorted_trains { };       ///< Train indices in spatial order (older files)
        std::vector<ObjectId> cable_lift_trains { }, thrill_lift_trains { }, reverse_cable_lift_trains { };

        // Flat rides
        i32 ride_state = 0; ///< 0 idle; 2 and 3 seen
        i32 people_on_ride = 0;
        i32 loop_count = 0;

        // Special attractions
        units::Money total_withdrawn { }; ///< Cash points
        units::Money average_withdrawn { };
    };

    struct StallItem
    {
        ObjectId id = 0;
        ObjectId item_asset = 0;     ///< Carried-item asset entry
        std::string kind_symbol { }; ///< That entry's symbol, as `Soda:cid`; empty when unresolved
        units::Money price { };
        i64 total_sold = 0;
        std::string name { };
        ObjectId stall = 0;     ///< Back reference as stored
        ObjectId sale_item = 0; ///< Sale prototype object, 0 if none
        RideColours colours { };
        u32 extras = 0; ///< Bit i: the i'th extra its kind can take goes on it
    };

    struct StallExtra
    {
        ObjectId id = 0;
        ObjectId extra_asset = 0;    ///< Extra-kind asset entry
        std::string kind_symbol { }; ///< That entry's symbol, as `Ketchup:ced`; empty when unresolved
        static constexpr i32 kNone = 0;
        static constexpr i32 kSome = 2;        ///< What a new stall starts at
        static constexpr i32 kGuestChoice = 5; ///< On every item that takes it, as much as the guest wants
        /// How much of it goes on: 0 none, then a little, some, lots, loads, and 5 as the guest picks.
        i32 level = 0;
    };

    struct Stall
    {
        AttractionCommon base { };
        std::vector<IntervalStats> intervals { };
        std::vector<ThoughtEntry> thoughts { };
        std::vector<StallItem> items { };
        std::vector<StallExtra> extras { };
        /// Whether a change to its menu or extras goes to every stall of its kind.
        bool sync = false;
        /// Of a custom stall, whose own kind sells nothing: the kind it sells as, by asset symbol as `<name>:sta`
        /// and overlay path, the player's pick. Empty while none is picked, and for other stalls.
        std::string sells_as { }, sells_as_overlay { };
    };

    /// Consistency counts gathered while loading. All are zero in sound files.
    struct RideDiagnostics
    {
        usize track_back_link_mismatch = 0; ///< track.ride != ride
        usize unresolved_tracks = 0;        ///< tracked ride whose Track does not resolve
        usize truncated_tracks = 0;         ///< chain walk ended early
        usize segment_piece_mismatch = 0;   ///< walked count != pieces naming the segment
        usize piece_segment_mismatch = 0;   ///< piece.segment id != segment it was reached from
        usize platform_back_link_mismatch = 0;
        usize open_ends_with_live_link = 0; ///< non-circuit track whose end links resolve to a piece
        usize train_back_link_mismatch = 0;
        usize train_index_mismatch = 0;
        usize car_back_link_mismatch = 0;
        usize car_index_mismatch = 0;
        usize car_count_mismatch = 0;
        usize unresolved_trains = 0;
        usize unresolved_cars = 0;
        usize duplicate_refs = 0;      ///< list entries naming an object already taken
        usize queue_size_mismatch = 0; ///< QueueSize != tiles pointing at the line
        usize item_back_link_mismatch = 0;
        usize unresolved_items = 0;
    };

    struct RideModel
    {
        std::vector<Ride> rides { };   ///< Tracked, flat and special attractions in file order
        std::vector<Stall> stalls { }; ///< In file order
        usize orphan_pieces = 0;       ///< Track pieces in the file that no track reaches
        RideDiagnostics diagnostics { };
    };

    /// A ride's place in RideModel::rides.
    using RideIndex = core::Id<struct RideIndexTag>;
    /// A stall's place in RideModel::stalls.
    using StallIndex = core::Id<struct StallIndexTag>;

    /// A trick the performers of a show ride know.
    struct ShowTrick
    {
        std::string name { };   ///< String key of its name, as "DolphinShowAnimTrick1"
        f32 seconds = 0;        ///< How long it lasts
        bool hidden = false;    ///< Kept out of the lists
        bool available = false; ///< Offered in this park
        bool invented = false;  ///< Researched
    };

    /// What a show ride puts on: the tricks its performers know, the first being what they do between
    /// shows, and the tricks of a show in the order they come, by place in `tricks`.
    struct RideShow
    {
        ObjectId ride = 0; ///< The ride's id (AttractionCommon::id)
        std::vector<ShowTrick> tricks { };
        std::vector<u32> order { };
    };

    /// The shows of the show rides in `file`. A trick the save names but does not hold is left out of the
    /// show; a show that knows no tricks is left out.
    [[nodiscard]] std::expected<std::vector<RideShow>, Error> load_shows(const SaveFile& file, const ObjectIndex& index);

    /// Builds the model from every ride, stall and facility object in `file`. Unknown or missing attributes
    /// take defaults, stale references load as 0 and the result is bounded by the size of the input. Fails only
    /// if an object cannot be decoded, which cannot happen for objects read_save returned.
    [[nodiscard]] std::expected<RideModel, Error> load_rides(const SaveFile& file, const ObjectIndex& index);

} // namespace rct3::save::model
