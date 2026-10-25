// SPDX-License-Identifier: GPL-3.0-or-later
//
// Brings the footpaths and stalls of an original save into the simulation, and sets the imported guests
// walking on those paths. Paths laid or taken away later come in and go the same way, as they change: walkers
// whose way crossed a tile taken away think again, and rides and stalls find their entrances and queue lines
// again.
#pragma once

#include "park/edge_walls.hpp"
#include "park/object_models.hpp"
#include "park/overhead.hpp"
#include "park/track_course.hpp"
#include "save/model/park.hpp"
#include "save/model/people.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"
#include "simulation/coaster.hpp"
#include "simulation/components.hpp"
#include "simulation/sim_world.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace rct3::simulation
{

    /// Adds a node for every ground, raised and queue path tile of `world_model`, laid on its terrain.
    void import_paths(SimWorld& world, const save::WorldModel& world_model);

    /// Adds the node of path tile `path`, laid on `grid`, and joins it to the paths beside it; for a node already
    /// there, takes the queue line the tile is in now. Call after import_paths, which sets the network's
    /// measures.
    NodeId add_path(SimWorld& world, const save::PathTile& path, const save::TerrainGrid& grid);

    /// Takes the node of path tile `path` away. Whoever stood on it moves to another path on its tile, if any;
    /// whoever was walking onto it or meant to cross it stops at the tile they came from and thinks again;
    /// litter on it goes with it.
    void remove_path(SimWorld& world, const save::PathTile& path, const save::TerrainGrid& grid);

    /// Has each imported ride and stall of `rides` find its entrance on the paths again, and each ride its
    /// queue line, after paths changed. A ride with guests in its queue keeps its line until the queue empties.
    /// Returns how many changed.
    std::size_t relink_attractions(SimWorld& world, const save::RideModel& rides);

    /// Adds a shop for every stall of `rides` that sells a drink or food and stands beside a path; returns how
    /// many were added. Call after import_paths.
    std::size_t import_stalls(SimWorld& world, const save::RideModel& rides);
    /// Has the shop of `stall`, when it runs, sell what its menu now sells first, at that item's price.
    void restock(SimWorld& world, const save::Stall& stall);

    /// How many a ride carries at once and how long one ride lasts: starting, `loops` of running, and stopping,
    /// in seconds. A ride timed by a whole lap runs one loop of it.
    struct RideTiming
    {
        std::uint32_t seats = 0;
        float start = 0;
        float run = 0;
        float stop = 0;
        std::uint32_t loops = 1;
    };

    /// The timing of a flat ride from its model: its seats, and its animations' lengths in the order the model
    /// lists them (standing, starting, running, stopping), the running one played `loops` times (at least
    /// once). A model without that cycle, as a maze or a show has, runs once for its longest animation.
    /// Nothing when the model has no seats or no animation that takes time.
    [[nodiscard]] std::optional<RideTiming> flat_ride_timing(std::span<const float> animation_seconds, std::uint32_t seats, std::int32_t loops);

    /// Has `ride` put on `show`: a go is the show's tricks one after another, with no starting or stopping. A
    /// show of no length leaves the ride as it was.
    void stage_show(CycleRide& ride, const save::RideShow& show);

    /// The timing of each flat ride of `rides` by its place in `rides.rides`, from the model of the object it
    /// stands as among `objects`, found by `model_of`; nothing for other rides and those without a usable model.
    [[nodiscard]] std::vector<std::optional<RideTiming>> flat_ride_timings(const save::RideModel& rides, std::span<const save::PlacedObject> objects, const std::function<const park::ObjectModel*(const save::PlacedObject&)>& model_of);

    /// The coaster model's view of `ride` on `course`: heights along it, its chains and stations, and the
    /// ride's speeds and first train's length as its park keeps them. Nothing unless the course goes round and
    /// has a station; shuttles, slides and open water are left to their timing.
    [[nodiscard]] std::optional<Coaster> coaster_of(const park::TrackCourse& course, const save::Ride& ride);

    /// Boats for the boat-hire and water-transport rides of `added` (the entities import_rides gave `rides`),
    /// one for each train, leaving the dock where its course in `courses` ends and coming back where it starts
    /// (transport boats calling at the course's other stations on the way), and the park's water for them to
    /// float on. Rides without a course keep their timing alone; those whose track goes round follow it as trains.
    void import_boats(SimWorld& world, const save::RideModel& rides, std::span<const entt::entity> added, std::span<const std::optional<park::TrackCourse>> courses, const save::TerrainGrid& grid, std::span<const save::WaterBody> water);

    /// Links the rides of `added` whose stations, as their courses in `courses` run through them, stand on tiles
    /// side by side on the same level, so trains told to may leave them together.
    void link_stations(SimWorld& world, std::span<const entt::entity> added, std::span<const std::optional<park::TrackCourse>> courses, const save::TerrainGrid& grid);

    /// The saved litter lying on the paths, each piece on the path tile under it, aged from when it was dropped
    /// to `now` (calendar units, as the park keeps them). Returns how many pieces came over. Call after
    /// import_paths.
    std::size_t import_litter(SimWorld& world, const save::PeopleModel& people, float now);

    /// The bins, benches and signs, each standing on the placed object of `objects` it names, that stand on a path tile, with what each bin holds;
    /// a no-entry sign bars its side of its tile to guests. Returns how many came over. Call after import_paths.
    /// shortcut: lamps carry nothing in saves that marks them out, so vandals leave them be; upgrade when path
    /// items are told apart by their models.
    std::size_t import_path_items(SimWorld& world, std::span<const save::PlacedObject> objects, std::span<const save::BinState> bins, std::span<const std::uint64_t> benches, std::span<const save::SignState> signs);
    /// Stands `wall` between the path tiles either side of it, or takes it away again when not `standing`.
    void set_wall(SimWorld& world, const park::EdgeWall& wall, bool standing);

    /// A path item of `kind` standing as `object` does, on the path tile under its tile's middle at the level
    /// nearest its base: a bin comes empty, holding kNewBinPieces. entt::null off the paths or for a hidden object.
    entt::entity add_path_item(SimWorld& world, const save::PlacedObject& object, PathItemKind kind);
    /// What a bin the player places holds, in pieces of rubbish, as the bins saved in the game's parks do.
    inline constexpr float kNewBinPieces = 40;
    /// A piece of rubbish in a saved bin's measure.
    /// Approximation: Fun Valley's saved bins hold 0.4 and fill in steps of a hundredth, so a piece of rubbish is
    /// taken to be one.
    inline constexpr float kSavedBinPiece = 0.01f;

    /// The park's staff, walking the paths at their work from where they stand; each one's entity by their
    /// place in `people.staff`; one off the paths or on a queue starts from the nearest walkway, null only when the
    /// park has none. Call after import_paths.
    std::vector<entt::entity> import_staff(SimWorld& world, const save::PeopleModel& people);

    /// The facility of a special attraction's kind, as its item in the game's files stores it; nothing for
    /// kinds guests do not use yet.
    [[nodiscard]] std::optional<Product> facility_of(std::uint32_t kind);
    /// The park's toilets, cash machines and first aid rooms, as shops guests go to by their needs; `kind_of`
    /// gives a special attraction's kind from the game's files. Returns how many came over. Call after
    /// import_paths.
    std::size_t import_facilities(SimWorld& world, const save::RideModel& rides, const std::function<std::optional<std::uint32_t>(const save::Ride&)>& kind_of);
    /// The objects `sight_of` gives a kind (Sight for fountains, effects and animatronics, or PhotoSpot), each
    /// on the path tile it stands on or one beside it, for guests to stop at. Returns how many came over. Call
    /// after import_paths.
    std::size_t import_sights(SimWorld& world, std::span<const save::PlacedObject> objects, const std::function<std::optional<PathItemKind>(const save::PlacedObject&)>& sight_of);
    /// The park entrances `squares_of` picks out, giving the tiles each covers along its own X and Z: the first, at
    /// the path tile it stands on or one beside it, as the gate guests come in and leave by; and the path tiles
    /// under every one of them as EntrancePaths. entt::null when none is on the paths. Call after import_paths,
    /// then keep_staff_in.
    entt::entity import_gate(SimWorld& world, std::span<const save::PlacedObject> objects, const std::function<std::optional<std::array<std::int32_t, 2>>(const save::PlacedObject&)>& squares_of);
    /// Keeps staff off the path tiles on land the park does not own and off those under its entrances, and lets
    /// them onto the others. Call after import_gate, and again when the park's land changes.
    void keep_staff_in(SimWorld& world, const save::TerrainGrid& grid);
    /// The side of its path tile, as the network numbers them, that a no-entry sign standing as `sign` bars: the
    /// side its front faces.
    [[nodiscard]] std::size_t no_entry_side(const save::PlacedObject& sign);

    /// Sets the park's own mix of parties (PartyMix) from the scenario's, when the save carries one.
    void import_party_mix(SimWorld& world, const save::GroupMix& mix);

    /// Marks the path tiles with a roof or shade over them, as `overhead` finds over the middle of each (see
    /// park/overhead.hpp). Returns how many are under a roof. Call after import_paths.
    std::size_t import_cover(SimWorld& world, const std::function<std::vector<park::Overhead>(std::span<const glm::vec3>)>& overhead);

    /// A saved ride's reliability, whether it stands broken down, and its inspections.
    [[nodiscard]] Upkeep upkeep_of(const save::Ride& ride);

    /// When a ride with `options` leaves the station. A load the game does not list waits for any load.
    [[nodiscard]] Departure departure_of(const save::OperatingOptions& options);

    /// The speed a ride's trains are launched to, in the launch modes that run round a circuit.
    [[nodiscard]] std::optional<float> launch_of(const save::Ride& ride);

    /// `ride`'s trains on `coaster`'s track, each where the park kept it.
    [[nodiscard]] Trains trains_of(const Coaster& coaster, const save::Ride& ride);

    /// The guests each ride of `rides` makes room for in the park, by its place in `rides.rides`, as its kind's
    /// item in the overlay it names says. `find` finds an overlay's file in the install from its path relative
    /// to it; nothing where the file or the item is missing.
    [[nodiscard]] std::vector<std::optional<std::uint32_t>> ride_guests(const save::RideModel& rides, const std::function<std::optional<std::filesystem::path>(std::string_view)>& find);
    /// The attraction kind of each ride of `rides`, found as `ride_guests` finds its guests.
    [[nodiscard]] std::vector<std::optional<std::uint32_t>> ride_kinds(const save::RideModel& rides, const std::function<std::optional<std::filesystem::path>(std::string_view)>& find);

    /// Adds a ride for every flat ride and coaster of `rides` whose timing is known and whose entrance touches
    /// a queue or a path; returns each ride's entity by its place in `rides.rides`, null for those left out. A flat ride's timing is `flat_timings` at its place in
    /// `rides.rides`. A coaster with its track in `coasters` at its place runs its train round it; one without
    /// is timed by its last test lap. Either carries all its seats. Excitement, intensity and nausea are the
    /// saved ratings; for a ride the park has not rated, those of its last test or of a test run on its track
    /// (simulation/ratings.hpp). Tracked rides leave the station as their operating settings say. Each makes
    /// room in the park for `guests` at its place, when there; those left out add theirs to RoomElsewhere while
    /// open and working. Call after import_paths.
    std::vector<entt::entity> import_rides(SimWorld& world, const save::RideModel& rides, std::span<const std::optional<RideTiming>> flat_timings, std::span<const std::optional<Coaster>> coasters = { }, std::span<const std::optional<std::uint32_t>> guests = { });

    /// Sets every guest walking from the path they stand on, or from the nearest path for one saved off the paths;
    /// returns how many. Call after import_paths.
    std::size_t start_guests(SimWorld& world);

    /// Puts the guests the park had on a ride (`guests` by place in `people`, `rides` as import_rides made
    /// them) back on it: on a flat ride's turn, or on a train, sending off one that stands at the start. Those
    /// the ride has no room for walk off from its exit. Returns how many it seated. Call after start_guests.
    std::size_t board_saved_riders(SimWorld& world, const save::PeopleModel& people, std::span<const entt::entity> guests, const save::RideModel& saved, std::span<const entt::entity> rides);

} // namespace rct3::simulation
