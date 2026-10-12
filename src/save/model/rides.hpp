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

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

#include "save/save_file.hpp"
#include "save/value.hpp"

namespace rct3::save {

/// An object id from the save. 0 is none.
using ObjectId = std::uint64_t;

// ---------------------------------------------------------------------------------------------------------
// Enumerations (named values are the known ones; any other integer may appear and must be tolerated)

enum class AttractionState : std::int32_t { Closed = 0, Testing = 1, Open = 2 };

enum class OperatingMode : std::int32_t {
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

enum class BreakdownKind : std::int32_t {
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

enum class RepairStatus : std::int32_t {
  Fine = 0,
  AwaitingBreakdown = 1,
  MechanicCalled = 2,
  MechanicOnTheWay = 3,
  BeingFixed = 4,
};

/// Train state machine, 0..70 in known files. Values that are not named here (for example the lift helper
/// states, which are numbered but not individually named) are still valid values of the type.
enum class TrainState : std::int32_t {
  WaitingToLoad = 0,
  Loading = 1,
  ReadyToDepart = 2,
  RestraintsClosing = 3,
  DoorsClosing = 4,
  WaitingToDepart = 5,
  WaitingToDepartSyncTrains = 6,
  WaitingToDepartSyncStations = 7,
  WaitingForCableLiftAtStation = 8,
  EngagingCableLift = 9,
  AscendingCableLift = 10,
  DisengagingCableLift = 11,
  WaitingToGoAtCableLift = 12,
  TravellingToNextStation = 13,
  WaitingAtLiftHill = 14,
  StoppingAtBlockBrake = 15,
  WaitingAtBlockBrake = 16,
  WaitingForCableLiftAtBlockBrake = 17,
  WaitingAtTop = 18,
  StoppingOnHoldingPiece = 19,
  StoppedOnHoldingPiece = 20,
  WaitingOnHoldingPiece = 21,
  StoppingAtStation = 22,
  StoppedAtStation = 23,
  AligningAtStation = 24,
  WaitingToUnload = 25,
  WaitingToUnloadSyncTrains = 26,
  DoorsOpening = 27,
  RestraintsOpening = 28,
  Unloading = 29,
  // 30..44: helper-train states of the thrill lift and cable lift mechanisms
  Crashing = 45,
  OffTrack = 46,
  OffTrackReturning = 47,
  OffTrackAligning = 48,
  OffTrackEntering = 49,
  ThrillLiftEntering = 50,
  Crashed = 51,
  SlowingAtBlockBrake = 52,
  // 53..66: reverse cable lift counterparts and sync/waiting variants
  FillingWithWater = 67,
  HeldInvisibleOffTrack = 68,
  SlowingForObstruction = 70,
};

enum class CarState : std::int32_t {
  StartingTravel = 0,
  Travelling = 1,
  Slowing = 2,
  Aligning = 3,
  AligningSpinSwing = 4,
  Aligned = 5,
  Crashing = 6,
  OffTrack = 7,
  RestraintsClosing = 8,
  RestraintsClosed = 9,
  DoorsClosing = 10,
  DoorsClosed = 11,
  RestraintsOpening = 12,
  RestraintsOpen = 13,
  DoorsOpening = 14,
  DoorsOpen = 15,
  HeldInvisibleOffTrack = 16,
};

/// What kind of attraction object a Ride was loaded from.
enum class RideKind : std::uint8_t {
  Tracked,  ///< Coaster or other track-guided ride with trains
  Flat,     ///< Animated (flat) ride: no track
  Special,  ///< Special attraction: toilets, cash machines and similar facilities
};

// ---------------------------------------------------------------------------------------------------------
// Shared parts

/// Three colour slot indices, each 0..31 in valid files (stored values are kept).
using RideColours = std::array<std::uint8_t, 3>;

/// Counters and history that every attraction (ride or stall) carries.
struct AttractionCommon {
  ObjectId id = 0;
  ObjectId kind_asset = 0;       ///< Reference to the asset-database entry of the attraction's kind
  std::string symbol;            ///< Asset symbol of the kind, e.g. "<name>:<tag>"; compare case-insensitively
  std::string overlay_path;      ///< Asset overlay path (no extension)
  std::string name;              ///< User name, a text key, or a numbered placeholder
  AttractionState state = AttractionState::Closed;
  std::uint32_t flags = 0;       ///< Bit set; all bits preserved. 0x40 never opened, 0x200 low-reliability warning
  float build_time = 0;          ///< Game clock seconds when built
  std::int64_t total_guests = 0;
  std::int64_t total_profit = 0;       ///< Hundredths
  std::int64_t running_cost = 0;       ///< Assessed running cost per interval, hundredths
  float popularity = 0;
  float satisfaction = 0;
  RepairStatus repair_status = RepairStatus::Fine;
  float time_since_inspection = -1;    ///< Seconds; -1 never
  float time_since_breakdown = -1;     ///< Seconds; -1 never
  float time_since_crash = -1;         ///< Seconds; -1 never
  ObjectId placement = 0;              ///< World placement record (stalls, flat rides, facilities), 0 if none
  std::uint8_t facing = 0;             ///< Quarter turns, 0..3
};

/// One of the ten statistics slots. Stored as a ring whose head is not identified, so file order is kept.
struct IntervalStats {
  std::int64_t guests = 0;
  std::int64_t income = 0;  ///< Hundredths
  std::int64_t losses = 0;  ///< Hundredths
};

// ---------------------------------------------------------------------------------------------------------
// Ride statistics and ratings

/// Results of one measured lap. All force values are accelerations in m/s^2.
struct TestResults {
  float max_speed = 0;
  float average_speed = 0;
  float ride_time = 0;    ///< Seconds
  float ride_length = 0;  ///< Metres
  float max_positive_vertical = 0;
  float max_negative_vertical = 0;
  float max_lateral = 0;
  float air_time = 0;     ///< Seconds
  std::int32_t drop_count = 0;
  float highest_drop = 0; ///< Metres
  std::int32_t inversion_count = 0;
  std::int32_t lift_hill_count = 0;
  /// Corner counts by turned angle: index 0..3 = 90, 180, 270, 360 degrees.
  std::array<std::int32_t, 4> corners_flat{};
  std::array<std::int32_t, 4> corners_banked{};
  std::array<std::int32_t, 4> corners_sloped{};
  /// Special section counts: water splash, rapids, spinning tunnel, waterfall, turntable, whirlpool, helices.
  std::array<std::int32_t, 7> special_sections{};
  bool valid = false;  ///< Only bit 0 of the stored word is meaningful; the rest is uninitialised memory
};

struct Assessment {
  bool valid = false;  ///< When false the other fields are not meaningful and are zero
  float excitement = 0;
  float intensity = 0;
  float nausea = 0;
  float reliability_base = 0;
  float under_cover = 0;  ///< Fraction 0..1
  float value = 0;        ///< Score
};

struct RideStats {
  std::optional<TestResults> current_lap;  ///< Accumulators of the lap in progress
  std::optional<TestResults> last_lap;     ///< Last completed valid lap
  std::optional<TestResults> combined;     ///< Multi-station result, newer files only
};

/// Per-ride history graphs of the last test lap (samples; at most 200).
struct RideGraphs {
  std::vector<float> speed, acceleration, altitude, vertical_g, lateral_g;
};

struct Breakdown {
  BreakdownKind current = BreakdownKind::None;
  BreakdownKind last = BreakdownKind::None;
  std::int32_t station = -1;  ///< -1 none
  std::int32_t train = -1;
  std::int32_t car = -1;
  std::int32_t seat = -1;
  std::int32_t inspection_interval = 0;
  float reliability = 100;  ///< Percent 0..100
  /// Eight window counters, each 0..20.
  std::array<std::uint8_t, 8> downtime_windows{};
  /// Downtime percentage derived from the windows: sum * 100 / 160.
  [[nodiscard]] float downtime_percent() const noexcept;
};

struct OperatingOptions {
  bool block_sectioned = false;
  bool sync_trains = false;
  bool leave_if_other_train_enters_station = false;
  std::int32_t min_wait = -1;   ///< Seconds; -1 none
  std::int32_t max_wait = -1;   ///< Seconds; -1 none
  std::int32_t wait_for_load = 0;
  float launch_speed = 0;       ///< m/s
  float lift_speed = 0;         ///< m/s
  float brake_speed = 0;        ///< m/s
  float block_brake_off_speed = 0;
  float fixed_speed = -1;       ///< m/s; -1 none
};

struct RaceState {
  bool in_progress = false;
  bool over = false;
  std::int32_t winner = -1;       ///< Train index, -1 none
  float seconds_since_end = 0;
};

/// A guest's recent opinion of the attraction.
struct ThoughtEntry {
  ObjectId guest = 0;
  std::int32_t thought = 0;  ///< Index into the game's thought table (meaning not decoded here)
  float time = 0;
  std::int64_t variant = 0;
};

// ---------------------------------------------------------------------------------------------------------
// Stations and queues

struct Station {
  ObjectId entrance = 0;  ///< Placement record; 0 when not built
  ObjectId exit = 0;
  float entrance_timer = 0;
  float exit_timer = 0;
  ObjectId queue_line = 0;  ///< 0 when the line does not exist
  std::int32_t queue_size = 0;   ///< Stored size of the queue line (0 without one)
  std::int32_t queue_tiles = 0;  ///< Queue path tiles in the file that belong to the line
  float queue_time = 0;          ///< Estimated queue time, seconds
};

// ---------------------------------------------------------------------------------------------------------
// Track

/// Platform part of a station piece.
struct TrackPlatform {
  ObjectId id = 0;
  std::int32_t tile_x = 0, tile_z = 0;
  std::int32_t height = 0;
  std::uint8_t direction = 0;
  std::uint8_t corner = 0;
  std::uint32_t flags = 0;
  RideColours colours{};
};

struct TrackPiece {
  ObjectId id = 0;
  std::string symbol;          ///< Track section asset symbol: identifies the piece shape
  ObjectId asset = 0;          ///< Asset-database entry of the piece
  ObjectId scenery = 0;        ///< World object that carries the piece, 0 if none
  std::int32_t tile_x = 0;
  std::int32_t tile_z = 0;
  std::int32_t height = 0;     ///< Integer height level
  float height_adjust = 0;     ///< Newer files only
  std::uint8_t direction = 0;  ///< Quarter turns, 0..3
  std::uint8_t corner = 0;     ///< Sub-tile corner, 0..3
  bool reversed = false;       ///< Traversed against the section's authored direction
  float start_distance = 0;    ///< Metres from the start of the track, increasing along the chain
  float reverse_start_distance = 0;
  RideColours colours{};
  /// Per-piece operating parameters. Stored as plain integers; their meaning is only partly established.
  std::int32_t brake_speed = -1;
  std::int32_t holding_time = 0;
  std::int32_t speed_class = -1;
  std::optional<TrackPlatform> platform;  ///< Station pieces
  std::uint32_t segment = 0;              ///< Index of the owning segment within Track::segments
};

struct TrackSegment {
  ObjectId id = 0;
  std::size_t first_piece = 0;  ///< Range [first_piece, first_piece + piece_count) of Track::pieces
  std::size_t piece_count = 0;
  /// Number of pieces in the file that name this segment as owner (equals piece_count in sound files).
  std::size_t owned_pieces = 0;
  /// True when this segment's own last piece links to its first and back (a loop of its own, as in a ride
  /// made of several parallel closed loops).
  bool closed = false;
};

struct Track {
  ObjectId id = 0;
  ObjectId ride = 0;
  std::uint8_t facing = 0;
  bool prototype = false;
  RideColours colours{};
  std::vector<TrackSegment> segments;
  std::vector<TrackPiece> pieces;  ///< All pieces in travel order
  /// True when the whole chain closes on itself (last piece links to the first and back) or when every
  /// segment is a closed loop of its own. Derived from the chain; the stored circuit flag is not used
  /// because real files contradict it.
  bool circuit = false;
  /// True when the chain walk ended early: a link was unresolved, repeated a piece, or ran out of segment.
  bool truncated = false;
};

// ---------------------------------------------------------------------------------------------------------
// Trains and cars

struct Car {
  ObjectId id = 0;
  std::int32_t index = 0;      ///< Position in the train as stored
  std::int32_t car_count = 0;  ///< Cars in the train as stored on the car
  /// Which of the (up to six) car designs of the train asset this car uses. Link cars use a distinct slot.
  std::int32_t design_slot = 0;
  CarState state = CarState::StartingTravel;
  std::int32_t passengers = 0;
  float distance = 0;          ///< Metres along the track
  float speed = 0;             ///< m/s
  float length = 0;            ///< Metres
  float mass = 0;
  bool reversed = false;
  float front_wheel_distance = 0;
  float rear_wheel_distance = 0;
  std::array<float, 3> position{};  ///< World position in metres (valid when position_valid)
  bool position_valid = false;
  ObjectId front_piece = 0;
  ObjectId rear_piece = 0;
};

struct Train {
  ObjectId id = 0;
  ObjectId ride = 0;           ///< Back reference as stored
  std::int32_t index = 0;      ///< Index as stored; equals the position in Ride::trains in sound files
  TrainState state = TrainState::WaitingToLoad;
  float state_time = 0;        ///< Seconds in the current state
  ObjectId front_piece = 0;
  ObjectId rear_piece = 0;
  float distance = 0;          ///< Metres along the track
  float speed = 0;             ///< m/s
  float acceleration = 0;
  std::int32_t station = -1;          ///< Station indices, -1 none
  std::int32_t start_station = -1;
  std::int32_t current_station = -1;
  std::int32_t lap_count = 0;
  std::int32_t shuttle_direction = 0;  ///< 0 or 1
  float static_friction = 0;
  float dynamic_friction = 0;
  float mass = 0;
  float length = 0;            ///< Metres
  std::string symbol;          ///< Train design asset symbol
  std::string overlay_path;
  std::vector<Car> cars;       ///< Front to back
};

// ---------------------------------------------------------------------------------------------------------
// Rides and stalls

struct Ride {
  RideKind kind = RideKind::Tracked;
  AttractionCommon base;
  OperatingMode mode = OperatingMode::ContinuousCircuit;
  std::int64_t ticket_price = 0;  ///< Hundredths
  std::vector<Station> stations;
  std::vector<IntervalStats> intervals;
  std::vector<ThoughtEntry> thoughts;
  Assessment assessment;
  RideStats stats;
  RideGraphs graphs;
  Breakdown breakdown;
  OperatingOptions options;
  RaceState race;
  RideColours car_colours{}, fence_colours{}, queue_colours{};
  ObjectId fence = 0;           ///< Fence asset entries, 0 if none
  ObjectId override_fence = 0;
  std::string platform_style;   ///< Tracked rides
  std::string platform_type;

  // Tracked rides
  ObjectId track_id = 0;
  std::optional<Track> track;        ///< Absent when the reference does not resolve
  std::vector<Train> trains;
  std::int32_t train_count = -1;     ///< Requested; -1 never set
  std::int32_t cars_per_train = -1;  ///< Requested passenger cars per train; -1 never set
  std::int32_t train_selection = 0;  ///< Index of the chosen train design
  struct Seat {
    std::int32_t car = 0, seat = 0, train = 0;
  };
  std::vector<Seat> seats;
  std::vector<ObjectId> block_sections;      ///< Occupying train per block section; 0 free
  std::vector<std::int32_t> sorted_trains;   ///< Train indices in spatial order (older files)
  std::vector<ObjectId> cable_lift_trains, thrill_lift_trains, reverse_cable_lift_trains;

  // Flat rides
  std::int32_t ride_state = 0;       ///< 0 idle; 2 and 3 seen
  std::int32_t people_on_ride = 0;
  std::int32_t loop_count = 0;

  // Special attractions
  std::int64_t total_withdrawn = 0;    ///< Cash points, hundredths
  std::int64_t average_withdrawn = 0;  ///< Hundredths
};

struct StallItem {
  ObjectId id = 0;
  ObjectId item_asset = 0;      ///< Carried-item asset entry
  std::int64_t price = 0;       ///< Hundredths
  std::int64_t total_sold = 0;
  std::string name;
  ObjectId stall = 0;           ///< Back reference as stored
  ObjectId sale_item = 0;       ///< Sale prototype object, 0 if none
  RideColours colours{};
};

struct StallExtra {
  ObjectId id = 0;
  ObjectId extra_asset = 0;  ///< Extra-kind asset entry
  /// Stored supply level. Known label order (none, little, some, more, loads, guest choice) is unconfirmed,
  /// so only the raw integer is kept.
  std::int32_t level = 0;
};

struct Stall {
  AttractionCommon base;
  std::vector<IntervalStats> intervals;
  std::vector<ThoughtEntry> thoughts;
  std::vector<StallItem> items;
  std::vector<StallExtra> extras;
};

/// Consistency counts gathered while loading. All are zero in sound files.
struct RideDiagnostics {
  std::size_t track_back_link_mismatch = 0;     ///< track.ride != ride
  std::size_t unresolved_tracks = 0;            ///< tracked ride whose Track does not resolve
  std::size_t truncated_tracks = 0;             ///< chain walk ended early
  std::size_t segment_piece_mismatch = 0;       ///< walked count != pieces naming the segment
  std::size_t piece_segment_mismatch = 0;       ///< piece.segment id != segment it was reached from
  std::size_t platform_back_link_mismatch = 0;
  std::size_t open_ends_with_live_link = 0;     ///< non-circuit track whose end links resolve to a piece
  std::size_t train_back_link_mismatch = 0;
  std::size_t train_index_mismatch = 0;
  std::size_t car_back_link_mismatch = 0;
  std::size_t car_index_mismatch = 0;
  std::size_t car_count_mismatch = 0;
  std::size_t unresolved_trains = 0;
  std::size_t unresolved_cars = 0;
  std::size_t duplicate_refs = 0;               ///< list entries naming an object already taken
  std::size_t queue_size_mismatch = 0;          ///< QueueSize != tiles pointing at the line
  std::size_t item_back_link_mismatch = 0;
  std::size_t unresolved_items = 0;
};

struct RideModel {
  std::vector<Ride> rides;    ///< Tracked, flat and special attractions in file order
  std::vector<Stall> stalls;  ///< In file order
  std::size_t orphan_pieces = 0;  ///< Track pieces in the file that no track reaches
  RideDiagnostics diagnostics;
};

/// Builds the model from every ride, stall and facility object in `file`. Unknown or missing attributes
/// take defaults, stale references load as 0 and the result is bounded by the size of the input. Fails only
/// if an object cannot be decoded, which cannot happen for objects read_save returned.
[[nodiscard]] std::expected<RideModel, Error> load_rides(const SaveFile& file, const ObjectIndex& index);

}  // namespace rct3::save
