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
//   "TERR" v1  save::TerrainGrid
//   "PATH" v1  the paths, std::vector<save::PathTile>
//   "OBJS" v1  the placed objects the player keeps, std::vector<save::PlacedObject>: those of the park and
//              those the player built; not the ones made again on loading (supports, railings, cars, trees)
//   "BINS" v1  std::vector<save::BinState>
//   "SIGN" v1  std::vector<save::SignState>
//   "RIDE" v1  save::RideModel
//   "PARK" v1  SavedProgress
//   "VIEW" v1  park::ParkCamera
//   "PEOP" v1  SavedPeople: the people's looks and pages, and where the guests and staff stand and how
//              they are. Saves made before it lack it; their people start again from the original park.
//   "QSGN" v1  std::vector<save::ObjectId>: the rides whose queues have no signs, by id. Only when there are any.
//   "SHOW" v1  std::vector<save::RideShow>: the show rides' shows. Only when there are any; without it, the
//              shows are read again from the original park.
//   "GOAL" v1  SavedGoals: how far the scenario's goals have got. Only for parks that set goals; without it,
//              they start again from the original park.
//   "BRKN" v1  std::vector<std::uint64_t>: the path items vandals have broken, by object id. Only when there
//              are any.
//   "FALL" v1  std::vector<save::Waterfall>: the park's waterfalls, with those the player made or deleted.
//              Saves made before it lack it; their waterfalls are read again from the original park.
//   "DAYM" v1  simulation::DayMode: which part of the day the park keeps. Saves made before it lack it; they keep
//              the original park's mode.
//   "ANML" v1  SavedAnimals: the enclosures, animal houses, animals and their dung. Saves made before it lack
//              it; their enclosures are read again from the original park.
//   "POOL" v1  park::PoolMap: the swimming pools the player laid; their pieces are built again on loading.
//              Only when there are any.
//
// The rest of the simulation (queues, rides' runs, litter, staff jobs) starts again from the original park.
#pragma once

#include "park/camera.hpp"
#include "park/pools.hpp"
#include "save/model/park.hpp"
#include "save/model/people.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"
#include "save/summary.hpp"
#include "simulation/animals.hpp"
#include "simulation/components.hpp"
#include "simulation/day_clock.hpp"
#include "simulation/park_rating.hpp"
#include "simulation/sim_world.hpp"

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
        std::optional<simulation::RatingParts> rating_parts;
        float time = 0;
        float zero_time = 0;
        save::CalendarDate date;
        save::WeatherType weather = save::WeatherType::Unknown;
        std::optional<save::WeatherType> forecast;
        std::optional<float> temperature;
        float day_time = 0; ///< The day clock's time
    };

    /// A guest in the park.
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
    };

    struct SavedPeople
    {
        save::PeopleModel model; ///< Looks, names and pages; the guests no longer in the park marked out
        std::vector<SavedGuest> guests;
        std::vector<SavedWorker> staff;
    };

    /// How far the scenario's goals have got, and the loan terms their prizes may have changed.
    struct SavedGoals
    {
        save::ObjectiveSet objectives { };
        std::int32_t max_loan = 0;
        float interest_rate_percent = 0;
    };

    /// An animal house, and which of SavedPeople::staff keeps it; -1 for none.
    struct SavedAnimalHouse
    {
        simulation::AnimalHouse house { };
        std::int32_t keeper = -1;
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
        save::EnclosureMap enclosures { };
        std::vector<SavedAnimalHouse> houses { };
        std::vector<SavedAnimal> animals { };
        std::vector<SavedDung> dung { };
    };

    struct SavedPark
    {
        std::vector<std::byte> base;
        save::SaveSummary summary;
        save::TerrainGrid grid;
        std::vector<save::PathTile> paths;
        std::vector<save::PlacedObject> objects;
        std::vector<save::BinState> bins;
        std::vector<save::SignState> signs;
        save::RideModel rides;
        SavedProgress progress;
        park::ParkCamera camera;
        std::optional<SavedPeople> people;          ///< None in saves made before people were kept
        std::vector<save::ObjectId> no_queue_signs; ///< Rides whose queues have no signs
        std::optional<std::vector<save::RideShow>> shows;
        std::optional<SavedGoals> goals;
        std::vector<std::uint64_t> broken_items;                ///< Path items vandals have broken, by object id
        std::optional<std::vector<save::Waterfall>> waterfalls; ///< None in saves made before waterfalls were kept
        std::optional<simulation::DayMode> day_mode;            ///< None in saves made before the mode was kept
        std::optional<SavedAnimals> animals;                    ///< None in saves made before animals were kept
        std::optional<park::PoolMap> pools;                     ///< The pools the player laid; none when they laid none
    };

    /// A guest's wallet as their saved page keeps it, each sum held to the page's range.
    [[nodiscard]] save::Cash kept_cash(const simulation::Wallet& wallet);

    /// The guests and staff of `world` as a save keeps them; `looks` gives the place in `model` of each one
    /// drawn (guests in model.guests, staff in model.staff).
    [[nodiscard]] SavedPeople keep_people(const simulation::SimWorld& world, save::PeopleModel model, const std::unordered_map<entt::entity, std::int32_t>& looks);

    /// Adds the saved guests to `world`, standing where they stood; returns them in the order of people.guests.
    /// Call after import_paths in place of import_guests, then start_guests, then place_parties.
    std::vector<entt::entity> place_guests(simulation::SimWorld& world, const SavedPeople& people);

    /// Makes the saved parties again of the guests start_guests set walking, `guests` being place_guests's
    /// answer; in place of import_parties.
    void place_parties(simulation::SimWorld& world, const SavedPeople& people, std::span<const entt::entity> guests);

    /// Adds the saved staff to `world`, walking; returns them in the order of people.staff, null for those not
    /// on a path. In place of import_staff.
    std::vector<entt::entity> place_staff(simulation::SimWorld& world, const SavedPeople& people);

    /// The enclosures and the animals of `world` as a save keeps them, the keepers named by their place in
    /// keep_people's staff.
    [[nodiscard]] SavedAnimals keep_animals(const simulation::SimWorld& world, save::EnclosureMap enclosures);

    /// Adds the saved animal houses, animals and dung to `world`, the houses' keepers being among `staff`,
    /// place_staff's answer.
    void place_animals(simulation::SimWorld& world, const SavedAnimals& animals, std::span<const entt::entity> staff);

    /// The objects a save keeps: the first `own` (the park's own), the scenery the player `placed`, and those a
    /// path, a track piece or a ride placed whole stands on, with its entrance and exit (the player's); not
    /// those made by the park itself (supports, railings, fences, cars, trees beyond the edge).
    [[nodiscard]] std::vector<save::PlacedObject> kept_objects(std::span<const save::PlacedObject> objects, std::size_t own, const std::unordered_set<std::uint64_t>& placed, std::span<const save::PathTile> paths, const save::RideModel& rides);

    [[nodiscard]] std::vector<std::byte> write_saved_park(const SavedPark& park);

    /// The saved park `bytes` hold, or why they do not hold one.
    [[nodiscard]] std::expected<SavedPark, std::string> read_saved_park(std::span<const std::byte> bytes);

    /// Whether `bytes` start as a park file does, rather than an original save.
    [[nodiscard]] bool is_park_file(std::span<const std::byte> bytes);

} // namespace rct3::parkfile
