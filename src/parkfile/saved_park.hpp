// SPDX-License-Identifier: GPL-3.0-or-later
//
// A park the player saved: the original park file it was started from, kept whole, and what the player has
// changed since, as the models stand when saved. Loading reads the original park and puts these over it.
//
// It is a park file (see park_file.hpp) with these chunks, each a layout hash then the value field by field
// (see binary.hpp):
//
//   "BASE" v1  the original park file's bytes
//   "SUMM" v1  save::SaveSummary, for the list of saves
//   "TERR" v1  save::model::TerrainGrid
//   "PATH" v1  the paths, std::vector<save::model::PathTile>
//   "OBJS" v1  the placed objects the player keeps, std::vector<save::model::PlacedObject>: those of the park and
//              those the player built; not the ones made again on loading (supports, railings, cars, trees)
//   "BINS" v1  std::vector<save::model::BinState>
//   "SIGN" v1  std::vector<save::model::SignState>
//   "RIDE" v1  save::model::RideModel
//   "PARK" v1  SavedProgress
//   "VIEW" v1  park::ParkCamera
//   "PEOP" v3  SavedPeople: the people's looks and pages, and where the guests and staff stand and how
//              they are. Saves made before it lack it; their people start again from the original park.
//   "QSGN" v1  std::vector<save::model::ObjectId>: the rides whose queues have no signs, by id. Only when there are any.
//   "SHOW" v1  std::vector<save::model::RideShow>: the show rides' shows. Only when there are any; without it, the
//              shows are read again from the original park.
//   "GOAL" v1  SavedGoals: how far the scenario's goals have got. Only for parks that set goals; without it,
//              they start again from the original park.
//   "BRKN" v1  std::vector<std::uint64_t>: the path items vandals have broken, by object id. Only when there
//              are any.
//   "FALL" v1  std::vector<save::model::Waterfall>: the park's waterfalls, with those the player made or deleted.
//              Saves made before it lack it; their waterfalls are read again from the original park.
//   "DAYM" v1  simulation::DayMode: which part of the day the park keeps. Saves made before it lack it; they keep
//              the original park's mode.
//   "ANML" v1  SavedAnimals: the enclosures, animal houses, animals and their dung. Saves made before it lack
//              it; their enclosures are read again from the original park.
//   "POOL" v9  park::pools::PoolMap: the swimming pools the player laid, their extras and changing rooms; their
//              pieces are built again on loading. Only when there are any.
//   "BEDS" v1  std::vector<save::model::FlowerBedState>: how much water each flower bed holds. Only when there are any;
//              without it, the beds' water is read again from the original park.
//
// The rest of the simulation (queues, rides' runs, litter, staff jobs) starts again from the original park.
#pragma once

#include "park/camera.hpp"
#include "park/pools/pools.hpp"
#include "save/model/park.hpp"
#include "save/model/people.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"
#include "save/summary.hpp"
#include "simulation/animals.hpp"
#include "simulation/components.hpp"
#include "simulation/day_clock.hpp"
#include "simulation/park/rating.hpp"
#include "simulation/sim_world.hpp"
#include "simulation/swimming.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rct3::parkfile
{

    /// Where the park's money and calendar stand.
    struct SavedProgress
    {
        std::optional<std::int64_t> cash; ///< Hundredths; none when money never runs out
        std::int64_t loan = 0;
        std::optional<float> rating;
        std::optional<simulation::park::RatingParts> rating_parts;
        float time = 0;
        float zero_time = 0;
        save::model::CalendarDate date;
        save::model::WeatherType weather = save::model::WeatherType::Unknown;
        std::optional<save::model::WeatherType> forecast;
        std::optional<float> temperature;
        float day_time = 0;       ///< The day clock's time
        std::int32_t climate = 0; ///< simulation::Climate
        float weather_hold = 0;   ///< Calendar units before the forecast arrives
        float wind = 0;
        float wind_change = 0;
        std::vector<save::model::LedgerPeriod> ledger; ///< The months' accounts, newest first
    };

    /// A guest in the park.
    /// A guest in a pool: the tile of the changing room they came in by, and how far they are with their swim.
    struct SavedSwim
    {
        std::int32_t room_col = 0;
        std::int32_t room_row = 0;
        simulation::Swimmer swimmer { };
    };

    struct SavedGuest
    {
        std::int32_t look = -1; ///< Place in SavedPeople::model.guests of the guest drawn for them; -1 none
        simulation::Visitor visitor;
        glm::vec3 position { 0.0f };
        float heading = 0;
        simulation::Needs needs;
        simulation::Mood mood;
        std::int64_t cash = 0;
        std::optional<simulation::Taste> taste { };
        std::int32_t party = -1; ///< Guests with the same number go round together; -1 alone
        bool leader = false;
        std::optional<SavedSwim> swim { };              ///< In a pool
        std::optional<simulation::Outside> outside { }; ///< On the way between a spawn point and the gate
    };

    /// A member of staff at work.
    struct SavedWorker
    {
        std::int32_t look = -1; ///< Place in SavedPeople::model.staff; -1 none
        simulation::StaffRole role = simulation::StaffRole::Other;
        std::int64_t wage = 0;
        std::int32_t wage_level = 2;
        std::int32_t training = 0;
        std::int32_t warnings = 0;
        float happiness = 1;
        float laziness = 0;
        glm::vec3 position { 0.0f };
        float heading = 0;
        std::optional<simulation::Patrol> patrol { };
        std::uint32_t mended = 0;    ///< Broken path items mended
        float think = 0;             ///< Seconds to their next thought
        std::uint32_t miserable = 0; ///< Thoughts in a row had while miserable, towards quitting
    };

    struct SavedPeople
    {
        save::model::PeopleModel model; ///< Looks, names and pages; the guests no longer in the park marked out
        std::vector<SavedGuest> guests;
        std::vector<SavedWorker> staff;
    };

    /// How far the scenario's goals have got, and the loan terms their prizes may have changed.
    struct SavedGoals
    {
        save::model::ObjectiveSet objectives { };
        std::int32_t max_loan = 0;
        float interest_rate_percent = 0;
    };

    /// An animal house, and which of SavedPeople::staff keeps it; -1 for none, with the job they are at.
    struct SavedAnimalHouse
    {
        simulation::AnimalHouse house { };
        std::int32_t keeper = -1;
        simulation::KeeperJob job = simulation::KeeperJob::Idle;
        glm::vec3 goal { 0.0f };
        std::int32_t target = -1; ///< Of SavedAnimals::dung while clearing, ::animals while catching, ::houses while filling; -1 none
        float busy = 0;           ///< Seconds
    };

    /// An animal, standing where it stood.
    struct SavedAnimal
    {
        simulation::Animal animal { };
        glm::vec3 position { 0.0f };
        float heading = 0;
    };

    /// Dung left lying.
    struct SavedDung
    {
        std::int32_t enclosure = -1;
        glm::vec3 position { 0.0f };
    };

    struct SavedAnimals
    {
        save::model::EnclosureMap enclosures { };
        std::vector<SavedAnimalHouse> houses { };
        std::vector<SavedAnimal> animals { };
        std::vector<SavedDung> dung { };
    };

    struct SavedPark
    {
        std::vector<std::byte> base;
        save::SaveSummary summary;
        save::model::TerrainGrid grid;
        std::vector<save::model::PathTile> paths;
        std::vector<save::model::PlacedObject> objects;
        std::vector<save::model::BinState> bins;
        std::vector<save::model::SignState> signs;
        save::model::RideModel rides;
        SavedProgress progress;
        park::ParkCamera camera;
        std::optional<SavedPeople> people;                 ///< None in saves made before people were kept
        std::vector<save::model::ObjectId> no_queue_signs; ///< Rides whose queues have no signs
        std::optional<std::vector<save::model::RideShow>> shows;
        std::optional<SavedGoals> goals;
        std::vector<std::uint64_t> broken_items;                       ///< Path items vandals have broken, by object id
        std::optional<std::vector<save::model::Waterfall>> waterfalls; ///< None in saves made before waterfalls were kept
        std::optional<simulation::DayMode> day_mode;                   ///< None in saves made before the mode was kept
        std::optional<SavedAnimals> animals;                           ///< None in saves made before animals were kept
        std::optional<park::pools::PoolMap> pools;                     ///< The pools the player laid; none when they laid none
        std::vector<save::model::FlowerBedState> flower_beds;          ///< Their water, as the simulation has it
    };

    /// A guest's wallet as their saved page keeps it, each sum held to the page's range.
    [[nodiscard]] save::model::Cash kept_cash(const simulation::Wallet& wallet);

    /// The guests and staff of `world` as a save keeps them; `looks` gives the place in `model` of each one
    /// drawn (guests in model.guests, staff in model.staff).
    [[nodiscard]] SavedPeople keep_people(const simulation::SimWorld& world, save::model::PeopleModel model, const std::unordered_map<entt::entity, std::int32_t>& looks);

    /// Adds the saved guests to `world`, standing where they stood; returns them in the order of people.guests.
    /// Call after import_paths in place of import_guests, then start_guests, then place_parties.
    std::vector<entt::entity> place_guests(simulation::SimWorld& world, const SavedPeople& people);

    /// Makes the saved parties again of the guests start_guests set walking, `guests` being place_guests's
    /// answer; in place of import_parties.
    void place_parties(simulation::SimWorld& world, const SavedPeople& people, std::span<const entt::entity> guests);

    /// Puts the saved swimmers back in the pools of their changing rooms, as they were; `guests` being
    /// place_guests's answer. Call after import_pools and start_guests. Returns how many.
    std::size_t place_swimmers(simulation::SimWorld& world, const SavedPeople& people, std::span<const entt::entity> guests);

    /// Adds the saved staff to `world`, walking; returns them in the order of people.staff, null for those not
    /// on a path. In place of import_staff.
    std::vector<entt::entity> place_staff(simulation::SimWorld& world, const SavedPeople& people);

    /// The enclosures and the animals of `world` as a save keeps them, the keepers named by their place in
    /// keep_people's staff.
    [[nodiscard]] SavedAnimals keep_animals(const simulation::SimWorld& world, save::model::EnclosureMap enclosures);

    /// Adds the saved animal houses, animals and dung to `world`, the houses' keepers being among `staff`,
    /// place_staff's answer.
    void place_animals(simulation::SimWorld& world, const SavedAnimals& animals, std::span<const entt::entity> staff);

    /// The objects a save keeps: the first `own` (the park's own), the scenery the player `placed`, and those a
    /// path, a track piece or a ride placed whole stands on, with its entrance and exit (the player's); not
    /// those made by the park itself (supports, railings, fences, cars, trees beyond the edge).
    [[nodiscard]] std::vector<save::model::PlacedObject> kept_objects(std::span<const save::model::PlacedObject> objects, std::size_t own, const std::unordered_set<std::uint64_t>& placed, std::span<const save::model::PathTile> paths, const save::model::RideModel& rides);

    [[nodiscard]] std::vector<std::byte> write_saved_park(const SavedPark& park);

    /// The saved park `bytes` hold, or why they do not hold one.
    [[nodiscard]] std::expected<SavedPark, std::string> read_saved_park(std::span<const std::byte> bytes);

    /// Whether `bytes` start as a park file does, rather than an original save.
    [[nodiscard]] bool is_park_file(std::span<const std::byte> bytes);

} // namespace rct3::parkfile
