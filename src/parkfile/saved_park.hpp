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
//   "PARK" v1  SavedProgress: the money, the calendar and the weather, with the ledger and the weekly graphs
//   "VIEW" v1  park::ParkCamera
//   "PEOP" v8  SavedPeople: the people's looks and pages, where the guests and staff stand and how they are,
//              the staff's jobs in hand, their passports, and the park's passport gift, with each one's entity and
//              dice. Saves made before it lack it, and v7 is skipped; their people start again from the original park.
//   "QSGN" v1  std::vector<save::model::ObjectId>: the rides whose queues have no signs, by id. Only when there are any.
//   "SHOW" v1  std::vector<save::model::RideShow>: the show rides' shows. Only when there are any; without it, the
//              shows are read again from the original park.
//   "GOAL" v1  SavedGoals: how far the scenario's goals have got. Only for parks that set goals; without it,
//              they start again from the original park.
//   "RSCH" v2  simulation::Research: how far research has got and what it has invented, when, and the focus.
//              Saves made before it lack it, and v1 is skipped; their research starts again from the original park.
//   "BRKN" v1  std::vector<std::uint64_t>: the path items vandals have broken, by object id. Only when there
//              are any.
//   "FALL" v1  std::vector<save::model::Waterfall>: the park's waterfalls, with those the player made or deleted.
//              Saves made before it lack it; their waterfalls are read again from the original park.
//   "DAYM" v1  simulation::DayMode: which part of the day the park keeps. Saves made before it lack it; they keep
//              the original park's mode.
//   "ANML" v5  SavedAnimals: the enclosures with their fences' wear, animal houses, animals and their dung, with
//              their care, and each animal's entity and dice. Saves made before it lack it, and v2 to v4 are skipped;
//              their enclosures are read again from the original park.
//   "POOL" v10 park::pools::PoolMap: the swimming pools the player laid, their extras and changing rooms; their
//              pieces are built again on loading. Only when there are any.
//   "BEDS" v1  std::vector<save::model::FlowerBedState>: how much water each flower bed holds. Only when there are any;
//              without it, the beds' water is read again from the original park.
//   "LITR" v1  std::vector<SavedLitter>: the litter and sick lying on the paths. Saves made before it lack it; their
//              litter is read again from the original park.
//   "RUNS" v1  std::vector<SavedRun>: where the rides are in their cycles, their trains and riders. Only when there are any.
//   "QUEU" v1  std::vector<SavedQueue>: who waits in each ride's queues, in order, and how long they have. Only when
//              anyone does; without it, the guests standing in a queue join it in the order they stand.
//   "CHTS" v1  bool, true: a cheat was used in the park. Only when one was.
//   "CAMP" v1  std::vector<save::model::Campaign>: the marketing campaigns running. Only when any are; without it,
//              none run.
//   "SIMS" v1  SavedSimulation: the run's seed, tick and gameplay rolls, and the dice of the attractions, the gates
//              and the fences. Saves made before it lack it; their run starts afresh from the seed it is opened with.
//
// The rest of the simulation (a mechanic on the way to a ride, a pool's rides) starts again from the original park.
#pragma once

#include "core/types.hpp"
#include "park/camera.hpp"
#include "park/pools/pools.hpp"
#include "random/generator.hpp"
#include "save/model/park.hpp"
#include "save/model/people.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"
#include "save/summary.hpp"
#include "simulation/animal_care.hpp"
#include "simulation/animals.hpp"
#include "simulation/components.hpp"
#include "simulation/day_clock.hpp"
#include "simulation/guests.hpp"
#include "simulation/park/rating.hpp"
#include "simulation/research.hpp"
#include "simulation/rides.hpp"
#include "simulation/sim_world.hpp"
#include "simulation/staff/patrol.hpp"
#include "simulation/swimming.hpp"
#include "units/units.hpp"

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
        std::optional<units::Money> cash; ///< None when money never runs out
        units::Money loan { };
        std::optional<f32> rating;
        std::optional<simulation::park::RatingParts> rating_parts;
        f32 time = 0;
        f32 zero_time = 0;
        save::model::CalendarDate date;
        save::model::WeatherType weather = save::model::WeatherType::Unknown;
        std::optional<save::model::WeatherType> forecast;
        std::optional<units::Degrees> temperature;
        f32 day_time = 0;
        i32 climate = 0;      ///< simulation::Climate
        f32 weather_hold = 0; ///< Calendar units before the forecast arrives
        f32 wind = 0;
        f32 wind_change = 0;
        std::vector<save::model::LedgerPeriod> ledger; ///< The months' accounts, newest first
        /// Hundredths; goal prizes may have raised it. Kept 32-bit while the park file layout may still change;
        /// once it is settled, this can become units::Money (64-bit) like the cash and loan.
        i32 max_loan = 0;
        f32 interest_rate_percent = 0;   ///< Goal prizes may have lowered it
        std::optional<i64> ledger_month; ///< The month the newest ledger period is for (simulation::park::ledger_month)
        /// The weekly graphs (simulation/park/graphs.hpp), the money's then the park's, as the models keep them.
        save::model::Series cash_less_loan, company_value, expenditure, income, park_value, share_price, weekly_profit;
        save::model::Series park_rating, guests_in_park, adults_in_park, teenagers_in_park, children_in_park, guest_intake, publicity_level;
        /// The week still running, for the graphs to count it whole: guests come in and attractions' takings so
        /// far, and the cash as it began (hundredths).
        i64 week_intake = 0, week_income = 0, week_cash = 0;
    };

    /// A guest in a pool: the tile of the changing room they came in by, and how far they are with their swim.
    struct SavedSwim
    {
        i32 room_col = 0;
        i32 room_row = 0;
        simulation::Swimmer swimmer { };
    };

    /// A guest in the park.
    struct SavedGuest
    {
        std::optional<i32> look { }; ///< Place in SavedPeople::model.guests of the guest drawn for them
        u32 entity = 0;              ///< Their entity in the run saved, which they get back when it is free
        std::optional<random::RandomState> dice { };
        simulation::Visitor visitor;
        glm::vec3 position { 0.0f };
        f32 heading = 0;
        simulation::Needs needs;
        simulation::Mood mood;
        i64 cash = 0;
        std::optional<simulation::Taste> taste { };
        std::optional<i32> party { }; ///< Guests with the same number go round together; none alone
        bool leader = false;
        std::optional<SavedSwim> swim { };              ///< In a pool
        std::optional<simulation::Outside> outside { }; ///< On the way between a spawn point and the gate
        std::optional<simulation::Passport> passport { };
        bool map = false;      ///< Carrying a park map
        bool umbrella = false; ///< Holding an umbrella
    };

    /// A member of staff at work.
    struct SavedWorker
    {
        std::optional<i32> look { }; ///< Place in SavedPeople::model.staff
        u32 entity = 0;              ///< Their entity in the run saved, which they get back when it is free
        std::optional<random::RandomState> dice { };
        simulation::StaffRole role = simulation::StaffRole::Other;
        i64 wage = 0;
        i32 wage_level = 2;
        i32 training = 0;
        i32 warnings = 0;
        f32 happiness = 1;
        f32 laziness = 0;
        glm::vec3 position { 0.0f };
        f32 heading = 0;
        std::optional<simulation::staff::Patrol> patrol { };
        u32 mended = 0;           ///< Broken path items mended
        units::Seconds think { }; ///< To their next thought
        u32 miserable = 0;        ///< Thoughts in a row had while miserable, towards quitting
        // The job in hand.
        simulation::StaffJob job = simulation::StaffJob::None;
        units::Seconds busy { };       ///< Left at it
        units::Seconds job_length { }; ///< As it began
        units::Share load { };         ///< Of the recent past, spent at work
        u64 ride = 0;                  ///< The saved id of the ride a mechanic works on; 0 for none
    };

    struct SavedPeople
    {
        save::model::PeopleModel model; ///< Looks, names and pages; the guests no longer in the park marked out
        std::vector<SavedGuest> guests;
        std::vector<SavedWorker> staff;
        std::optional<simulation::PassportRules> passport { }; ///< The park's gift and the stamps it takes, as the player set them
    };

    /// How far the scenario's goals have got.
    struct SavedGoals
    {
        save::model::ObjectiveSet objectives { };
    };

    /// An animal house, and which of SavedPeople::staff keeps it, if any, with the job they are at.
    struct SavedAnimalHouse
    {
        simulation::AnimalHouse house { };
        simulation::HouseCare care { };
        /// shortcut: a keeper's tool is not kept, so one comes back holding the trough filler; keep it if the
        /// hut walks after loading show.
        std::optional<i32> keeper { };
        simulation::KeeperJob job = simulation::KeeperJob::Idle;
        glm::vec3 goal { 0.0f };
        std::optional<i32> target { }; ///< Of SavedAnimals::dung while clearing, ::animals while catching, ::houses while filling
        units::Seconds busy { };
    };

    /// An animal, standing where it stood.
    struct SavedAnimal
    {
        simulation::Animal animal { };
        glm::vec3 position { 0.0f };
        f32 heading = 0;
        simulation::AnimalCare care { };
        u32 entity = 0; ///< Its entity in the run saved, which it gets back when it is free
        std::optional<random::RandomState> dice { };
    };

    /// Dung left lying.
    struct SavedDung
    {
        std::optional<i32> enclosure { };
        glm::vec3 position { 0.0f };
        u8 size = 0; ///< As Dung's
    };

    struct SavedAnimals
    {
        save::model::EnclosureMap enclosures { };
        std::vector<SavedAnimalHouse> houses { };
        std::vector<SavedAnimal> animals { };
        std::vector<SavedDung> dung { };
    };

    /// A piece of litter, or sick, lying on the paths.
    struct SavedLitter
    {
        glm::vec3 position { 0.0f }; ///< The middle of the path tile it lies on
        u8 kind = 0;                 ///< As simulation::Litter numbers it
        units::Seconds age { };
        bool vomit = false;
    };

    /// A guest waiting in a queue: their place in SavedPeople::guests, and how long they have waited.
    struct SavedQueuer
    {
        i32 guest = 0;
        units::Seconds queued { };
    };

    /// The queue of one station of a ride, first in line first.
    struct SavedQueue
    {
        u64 ride = 0;   ///< The ride's saved id (simulation::Attraction::saved)
        u8 station = 0; ///< 0 the first, k its k-th stop after it
        std::vector<SavedQueuer> guests { };
    };

    /// A guest on a ride: their place in SavedPeople::guests, and the stations they boarded at and get off at.
    struct SavedRider
    {
        i32 guest = 0;
        u8 station = 0;
        u8 off = 0;
    };

    /// A train on its track: how far along it is, how fast, and who rides it.
    struct SavedTrain
    {
        bool running = false;
        f32 at = 0;
        f32 travelled = 0;
        u32 next_stop = 0;
        f32 speed = 0;
        bool launched = false;
        units::Radians lean { };
        units::TurnRate lean_rate { };
        i32 passed = 0;
        f32 push = 0;
        units::Distance pushed { };
        units::Seconds held { };
        std::vector<SavedRider> riders { };
    };

    /// Where a ride is in its cycle, with its trains and riders.
    struct SavedRun
    {
        u64 ride = 0; ///< The ride's saved id (simulation::Attraction::saved)
        simulation::RidePhase phase = simulation::RidePhase::Loading;
        units::Seconds in_phase { };
        u32 loop = 0;
        units::Seconds loading { };
        std::optional<units::Seconds> since_departure { };
        f32 per_hour = 0;
        std::vector<units::Seconds> stops_loading { }; ///< By stop after the first station
        std::vector<SavedRider> riders { };            ///< Of a ride with no trains
        std::vector<SavedTrain> trains { };
    };

    /// Dice kept by what owns them.
    struct SavedDice
    {
        u64 owner = 0;
        random::RandomState dice { };
    };

    /// Where the park's run has got, so a park loaded goes on as the run saved would have: what its randomness
    /// starts from, the ticks run, and what the gameplay and each attraction, gate and the fences roll next. The
    /// people's and animals' dice are kept with them.
    struct SavedSimulation
    {
        random::Seed seed { };
        u64 tick = 0;
        random::RandomState gameplay { };
        units::Seconds unstepped { };           ///< Park time run that no tick has stepped yet
        std::vector<SavedDice> attractions { }; ///< By the attraction's saved id (simulation::Attraction::saved)
        std::vector<SavedDice> gates { };       ///< By the gate's path node
        std::optional<random::RandomState> fences { };
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
        std::optional<simulation::Research> research;                  ///< None in saves made before research was kept
        std::vector<u64> broken_items;                                 ///< Path items vandals have broken, by object id
        std::optional<std::vector<save::model::Waterfall>> waterfalls; ///< None in saves made before waterfalls were kept
        std::optional<simulation::DayMode> day_mode;                   ///< None in saves made before the mode was kept
        std::optional<SavedAnimals> animals;                           ///< None in saves made before animals were kept
        std::optional<park::pools::PoolMap> pools;                     ///< The pools the player laid; none when they laid none
        std::vector<save::model::FlowerBedState> flower_beds;          ///< Their water, as the simulation has it
        std::optional<std::vector<SavedLitter>> litter;                ///< None in saves made before litter was kept
        std::vector<SavedQueue> queues;                                ///< The queues anyone waits in
        std::vector<SavedRun> runs;                                    ///< Of the rides kept mid-cycle
        bool cheats_used = false;
        std::vector<save::model::Campaign> campaigns; ///< Those running
        std::optional<SavedSimulation> simulation;    ///< None in saves made before the run was kept
    };

    /// A guest's wallet as their saved page keeps it, each sum held to the page's range.
    [[nodiscard]] save::model::Cash kept_cash(const simulation::Wallet& wallet);

    /// The guests and staff of `world` as a save keeps them; `looks` gives the place in `model` of each one
    /// drawn (guests in model.guests, staff in model.staff).
    [[nodiscard]] SavedPeople keep_people(const simulation::SimWorld& world, save::model::PeopleModel model, const std::unordered_map<entt::entity, i32>& looks);

    /// Adds the saved guests to `world`, standing where they stood, each as the entity they were when it is free;
    /// returns them in the order of people.guests. Call after import_paths in place of import_guests, before
    /// anything else adds entities, then start_guests, then place_parties and place_guest_dice.
    std::vector<entt::entity> place_guests(simulation::SimWorld& world, const SavedPeople& people);

    /// Makes the saved parties again of the guests start_guests set walking, `guests` being place_guests's
    /// answer; in place of import_parties.
    void place_parties(simulation::SimWorld& world, const SavedPeople& people, std::span<const entt::entity> guests);

    /// Gives the guests back the dice they had, `guests` being place_guests's answer; after start_guests, which
    /// gives them new ones.
    void place_guest_dice(simulation::SimWorld& world, const SavedPeople& people, std::span<const entt::entity> guests);

    /// Puts the saved swimmers back in the pools of their changing rooms, as they were; `guests` being
    /// place_guests's answer. Call after import_pools and start_guests. Returns how many.
    usize place_swimmers(simulation::SimWorld& world, const SavedPeople& people, std::span<const entt::entity> guests);

    /// Adds the saved staff to `world`, walking, each as the entity and with the dice they had; returns them in the
    /// order of people.staff, null for those not on a path. In place of import_staff.
    std::vector<entt::entity> place_staff(simulation::SimWorld& world, const SavedPeople& people);

    /// The enclosures and the animals of `world` as a save keeps them, the keepers named by their place in
    /// keep_people's staff.
    [[nodiscard]] SavedAnimals keep_animals(const simulation::SimWorld& world, save::model::EnclosureMap enclosures);

    /// Adds the saved animal houses, animals and dung to `world`, the houses' keepers being among `staff`,
    /// place_staff's answer.
    void place_animals(simulation::SimWorld& world, const SavedAnimals& animals, std::span<const entt::entity> staff);

    /// Where the run of `world` has got, with `unstepped` park seconds left for the next tick.
    [[nodiscard]] SavedSimulation keep_simulation(const simulation::SimWorld& world, units::Seconds unstepped);

    /// Puts `world` back where the run saved had got: its tick and gameplay rolls, and the dice of the attractions,
    /// gates and fences. Call once the park is loaded; its seed is set before, as the park is made from it.
    void place_simulation(simulation::SimWorld& world, const SavedSimulation& saved);

    /// The litter lying on the paths of `world`.
    [[nodiscard]] std::vector<SavedLitter> keep_litter(const simulation::SimWorld& world);

    /// Lays the saved litter on the path tiles under it, in place of import_litter; returns how many pieces. Call
    /// after import_paths.
    usize place_litter(simulation::SimWorld& world, std::span<const SavedLitter> litter);

    /// The queues of the rides of `world` that anyone waits in, the guests numbered as keep_people orders them.
    [[nodiscard]] std::vector<SavedQueue> keep_queues(const simulation::SimWorld& world);

    /// Puts the guests that start_guests set queueing back in the order they waited, with the time they had
    /// waited, `guests` being place_guests's answer. A guest no longer on their ride's queue stays where
    /// start_guests put them; one not saved goes behind those who were. Returns how many were put back.
    usize place_queues(simulation::SimWorld& world, std::span<const SavedQueue> queues, std::span<const entt::entity> guests);

    /// Where the rides of `world` are in their cycles, the guests numbered as keep_people orders them. A ride
    /// whose trains have crashed, are off the track or up a lift tower, or a pool's ride, is left out.
    [[nodiscard]] std::vector<SavedRun> keep_runs(const simulation::SimWorld& world);

    /// Puts the rides back where they were in their cycles, their trains where they were on the track and their
    /// riders on them, `guests` being place_guests's answer. A ride whose trains are not as saved starts its cycle
    /// afresh. Call after start_guests. Returns how many riders were put back.
    usize place_runs(simulation::SimWorld& world, std::span<const SavedRun> runs, std::span<const entt::entity> guests);

    /// The objects a save keeps: the first `own` (the park's own), the scenery the player `placed`, and those a
    /// path, a track piece or a ride placed whole stands on, with its entrance and exit (the player's); not
    /// those made by the park itself (supports, railings, fences, cars, trees beyond the edge).
    [[nodiscard]] std::vector<save::model::PlacedObject> kept_objects(std::span<const save::model::PlacedObject> objects, usize own, const std::unordered_set<u64>& placed, std::span<const save::model::PathTile> paths, const save::model::RideModel& rides);

    [[nodiscard]] std::vector<std::byte> write_saved_park(const SavedPark& park);

    /// The saved park `bytes` hold, or why they do not hold one.
    [[nodiscard]] std::expected<SavedPark, std::string> read_saved_park(std::span<const std::byte> bytes);

    /// Whether `bytes` start as a park file does, rather than an original save.
    [[nodiscard]] bool is_park_file(std::span<const std::byte> bytes);

} // namespace rct3::parkfile
