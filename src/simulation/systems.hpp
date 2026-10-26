// SPDX-License-Identifier: GPL-3.0-or-later
//
// One step of the park's life. Each step:
//
//  1. needs grow and moods drift, for every guest;
//  2. guests on the move walk, and a guest at a path tile with nothing to do picks something: a shop for
//     what they need most, now and then a flat ride, or else the next tile to wander to;
//  3. guests who reached the attraction they were heading for buy there or join its queue;
//  4. flat rides run their cycles: a stopped ride takes on the front of its queue and runs, and lets its
//     riders off when the cycle ends;
//  5. parties whose riders are all off go on together again.
//
// Followers in a party do not choose anything: after phase 2 they take their leader's step, each in a lane
// of their own. Phases 1 and 2 and the followers' step only write each guest's own components and are shared
// out over the workers; phases 3 to 5 change attractions and parties and run on one thread in ascending
// entity order. Every choice draws on the guest's own
// Dice, so a world steps the same way however many workers it has.
#pragma once

#include "simulation/coaster.hpp"
#include "simulation/components.hpp"
#include "simulation/sim_world.hpp"
#include "simulation/tuning.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>

namespace rct3::simulation
{

    class Workers;

    inline constexpr float kTickSeconds = 0.25f; ///< Simulated time a step covers
    inline constexpr float kConsumeSeconds = 10; ///< A guest spends with what they bought before moving on; our own figure
    /// Farthest a guest walks off a path tile's middle, along x and z.
    /// shortcut: one width for every path, from how evenly saved guests spread to about this either side of
    /// the middle; upgrade when paths of other widths are modelled.
    inline constexpr float kLaneReach = 1.3f;
    inline constexpr float kLongQueue = 240; ///< Seconds of queueing that feel like ages

    struct Pose;
    /// Where `pose` is drawn `along` (0..1) of the way through the step after the last.
    [[nodiscard]] Pose between_steps(const Pose& pose, float along);

    /// The gait a guest of `age`, feeling as `mood` and `needs` have them, goes on in from a tile: worn out,
    /// sick or bursting they show it, and otherwise now and then they run, children most and the fresher the
    /// likelier. Drawn with `dice`.
    [[nodiscard]] Gait pick_gait(AgeGroup age, const Mood& mood, const Needs& needs, random::RandomState& dice);

    /// The ground covered in a second by a guest whose own walk is `walk`, with `energy` left, in `gait`.
    [[nodiscard]] Pace gait_pace(Pace walk, Share energy, Gait gait);

    /// A coaster rider's own leanings, each a share fixed for the rider: towards raising their hands, bracing
    /// against the forces and covering their eyes; the higher, the likelier.
    struct Leanings
    {
        float hands = 0;
        float brace = 0;
        float eyes = 0;
    };

    /// How a rider reacting as `now` reacts `dt` seconds on, as the train they ride feels (`running` it is off
    /// round the track, `lifted` up a lift chain): waiting at the station and up the lift; bracing against
    /// strong forces; covering their eyes going down a ride `over` (intensity past what they like) too wild for
    /// them; throwing up their hands when `happiness` thrills them or floating over a hill, more readily in
    /// the `front` car, and keeping them up till the forces grow too strong. Drawn with `dice`.
    [[nodiscard]] RideReaction react(RideReaction now, const Felt& feel, bool running, bool lifted, Share happiness, float over, const Leanings& leanings, bool front, random::RandomState& dice, float dt);

    /// Keeps a train this far behind the back of the one in front, metres; our own figure.
    inline constexpr float kBlockGap = 4.0f;

    /// The clear track ahead of train `t` of `trains`, up to kBlockGap short of the back of the nearest
    /// train in front; endless for a lone train.
    [[nodiscard]] float room_ahead(std::span<const Coaster> trains, std::size_t t);

    struct Departure;
    /// Whether a train (or a ride's next go) with `riders` of `capacity` seats aboard leaves after `waited`
    /// seconds of loading, as `departure` says: never before its least wait; then once it has its load, or
    /// whatever its load (even none) once its longest wait is up.
    [[nodiscard]] bool departs(const Departure& departure, std::size_t riders, std::size_t capacity, float waited);

    struct CycleRide;
    /// How long a whole cycle of `ride` lasts: starting, its loops of running, and stopping.
    [[nodiscard]] float cycle_seconds(const CycleRide& ride);
    /// How far into its cycle `ride` is; nothing while it loads.
    [[nodiscard]] std::optional<float> seconds_into_cycle(const CycleRide& ride);
    /// Moves a ride that has left on by `dt` seconds through starting, running and stopping. True when it has
    /// just stopped, back to loading, and its riders get off; a `held` ride (its restraints or doors stuck shut)
    /// stays stopped with its riders aboard till it is not.
    bool advance_cycle(CycleRide& ride, float dt, bool held);

    /// Pieces of litter lying long enough to count against the park's rating (tuning::Litter::old).
    [[nodiscard]] std::int32_t old_litter(const SimWorld& world);

    /// The broken path items, and what putting each back new comes to at `price` (by its object id, the placed
    /// object it stands as). Our own design, as replace_broken.
    struct Replacement
    {
        std::uint32_t items = 0;
        std::int64_t cost = 0; ///< Hundredths
    };
    [[nodiscard]] Replacement replacement(const SimWorld& world, const std::function<std::int64_t(std::uint64_t object)>& price);

    /// Puts every broken path item back new at once. The player pays the whole price of each (replacement), the
    /// quick and dear way; a handyman mends one for nothing, given time.
    void replace_broken(SimWorld& world);

    /// Takes the ride or shop `attraction` out of the park: its queue walks off from its entrance, its riders
    /// from its exit, and those on their way to it or waiting for it go on to something else.
    void pull_down(SimWorld& world, entt::entity attraction);

    /// Path tiles (not queues) that no member of staff in `role` has in their patrol. None when any of them has
    /// no patrol, as they walk the whole park, or when there is no one in the role.
    [[nodiscard]] std::size_t uncovered_paths(const SimWorld& world, StaffRole role);

    /// Adds the square of tiles `across` wide round `at` to `staff`'s patrol area, or takes them away, and does
    /// the same to everyone sharing their zone. An area left with no tiles goes: the whole park again.
    void paint_patrol(entt::registry& r, entt::entity staff, glm::ivec2 at, int across, bool add);

    /// Empties `staff`'s patrol area and their zone's: the whole park again. They stay on their zone.
    void clear_patrol(entt::registry& r, entt::entity staff);

    /// Fills the box round `staff`'s patrol area, for them and everyone sharing their zone.
    void fill_patrol(entt::registry& r, entt::entity staff);

    /// Puts `staff` on shared zone `zone` (0 none). Joining a zone others are on takes its tiles.
    void join_zone(entt::registry& r, entt::entity staff, std::uint32_t zone);

    /// Guests a second coming in at a gate in `weather`: fewer in rain, storms and the cold, more on sunny days.
    [[nodiscard]] double arrivals_per_second(const Weather& weather);

    /// Guests the park has room for, with some to spare: what its open rides that are not broken down make
    /// room for (or a share for each seat, where a ride's data does not say), and a share for each square of
    /// pool water, and the room of rides not simulated (RoomElsewhere).
    [[nodiscard]] std::uint32_t ideal_guests(const SimWorld& world);

    /// The entrance fee guests think fair, in hundredths: what the open rides are worth to them over what they
    /// charge.
    [[nodiscard]] std::int64_t fair_entrance_fee(const SimWorld& world);

    /// How many guests come in against a plain park, from the park's rating, how crowded it is (`guests`
    /// against `ideal`) and its entrance fee against `fair_fee`.
    [[nodiscard]] double drawn(const Draw& draw, std::uint32_t guests, std::uint32_t ideal, std::int64_t fair_fee);

    /// How often each PartyKind comes in at the gate against the others: the park's mix (PartyMix, else the
    /// tuning's), tilted toward teens by wild rides and toward families by gentle ones, and after dark mostly
    /// teens.
    [[nodiscard]] std::array<double, kPartyKinds> party_weights(const SimWorld& world, bool night);

    /// Whether a time of day (a fraction from midnight) is dark: from 20:00 to 6:00, our own hours.
    [[nodiscard]] bool is_night(float time);

    /// How exciting a guest of `age` finds a ride rated `excitement`: in the dark a coaster thrills teens more
    /// and families less.
    [[nodiscard]] float felt_excitement(float excitement, bool coaster, AgeGroup age, bool night);

    /// Ticks in `seconds` of simulated time, rounded up.
    [[nodiscard]] std::uint32_t ticks(float seconds);

    /// Gives guest `guest` what it needs to walk, standing on path node `at` with nothing to do. Its dice are
    /// seeded from the world's seed and its id, so the same park always plays out the same way.
    void start_walking(SimWorld& world, entt::entity guest, NodeId at);

    /// Brings a whole party in on path node `at`, of a kind the park's mix picks, as the gate does: a lone guest
    /// or a party, each walking with nothing to do yet. Returns its members, the leader first.
    std::vector<entt::entity> admit_party(SimWorld& world, random::RandomState& dice, NodeId at);

    /// Brings in `who` (each an age, and whether female) on path node `at` as one party, the first its leader,
    /// with tastes from `band`. Returns its members.
    std::vector<entt::entity> admit_guests(SimWorld& world, random::RandomState& dice, NodeId at, std::span<const std::pair<AgeGroup, bool>> who, const tuning::TasteBand& band);

    /// The player picks `person` up, guest or staff, out of what they were doing: a guest leaves their queue
    /// and their party, a mechanic the ride they were going to. False, and nothing done, for one who cannot be
    /// picked up now: on a ride, in a booth, gone home or held already.
    bool pick_up(SimWorld& world, entt::entity person);

    /// The player lets a held person go over `ground`, a point on the ground or a path: they fall to it and walk
    /// to the nearest path to go on from. False, and still held, where there are no paths to walk to.
    bool let_go(SimWorld& world, entt::entity person, glm::vec3 ground);

    /// Advances `world` by one tick, sharing work out over `workers` when given.
    void step(SimWorld& world, std::optional<Workers&> workers = std::nullopt);

} // namespace rct3::simulation
