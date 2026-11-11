// SPDX-License-Identifier: GPL-3.0-or-later
//
// Guests: who they are, what they need and feel, what they have to spend, and the steps that move them through
// a tick (needs, walking, following their party, spacing out and arriving where they head). The components
// here, the helpers the other systems share, and those steps.
#pragma once

#include "profiling/profiler.hpp"
#include "simulation/components.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace rct3::simulation::tuning
{
    struct TasteBand;
}

namespace rct3::simulation::staff
{
    struct Patrol;
}

namespace rct3::simulation
{

    struct SimWorld;
    class Workers;
    struct Ware;

    enum class AgeGroup : std::uint8_t
    {
        Child,
        Teen,
        Adult,
    };

    /// Someone visiting the park.
    struct Visitor
    {
        AgeGroup age = AgeGroup::Adult;
        bool female = false;
        std::string name;
        std::uint64_t arrived = 0; ///< Tick the visitor entered the park
        bool avatar = false;       ///< Made by the player, who keeps the photos taken of them
    };

    struct Needs
    {
        Share hunger { };
        Share thirst { };
        Share bladder { };
        Share swim { }; ///< Wish to go swimming, grown by how much they like pools
    };

    struct Mood
    {
        Share happiness { 0.5f };
        Share energy { 0.5f };
        Share nausea { };
        Share tolerance { 0.5f }; ///< How well the guest's stomach takes rides: the higher, the wilder the rides they go on and the slower they sicken
        Share wet { };            ///< From dry (0) to soaked (1)
        Share heat { };           ///< From cool (0) to sweltering (1), out in the hot sun
    };

    /// A place outside the park, mostly where its road leaves the map, where arriving guests appear and walk to
    /// the gate from, and where those going home walk off.
    struct GuestSpawn
    {
        NodeId node = kNoNode;
    };

    /// A guest on the paths outside the park, between a spawn point and the gate.
    struct Outside
    {
        enum class Way : std::uint8_t
        {
            In,         ///< Walking to the gate, not paid yet
            TurnedAway, ///< Could not pay at the gate, walking back
            Home,       ///< Walking home from the gate
        };
        Way way = Way::In;
    };

    /// Guests in and out of the gate so far, in the registry's context, by AgeGroup.
    struct Turnstile
    {
        std::array<std::uint32_t, 3> in { };
        std::array<std::uint32_t, 3> out { };
        std::uint32_t turned_away = 0; ///< Parties that could not pay to come in
    };

    /// The intensity of rides a guest likes, 0..1 as rides are rated; any when they have none.
    struct Taste
    {
        Share least { };
        Share most { 1 };
        Share pools { 0.5f }; ///< How much they like swimming, which their wish to swim grows by
    };

    /// A guest carrying an umbrella, up while it rains.
    struct Umbrella
    {
    };

    /// Food or drink a guest carries from a stall, in their left hand: eaten or drunk a bite at a time, then
    /// held as an empty (a wrapper, a cup) until they are rid of it: in a bin, in a pocket, or on the path.
    struct Carried
    {
        static constexpr std::uint8_t kPockets = 2; ///< As saved guests have: a left and a right one

        entt::entity stall = entt::null; ///< Where it came from, for what it looks like
        std::uint8_t ware = 0;           ///< Its place on that stall's menu
        std::uint8_t kind = 0;           ///< As Litter::kind, once dropped
        bool food = true;                ///< Else drink
        std::uint8_t bites = 0;          ///< Left; none once it is an empty
        Share fill { };                  ///< Of their hunger or thirst each bite takes away
        Seconds since { };               ///< Since the last bite, or since it was finished
        bool held = true;                ///< In hand; else only empties in their pockets are left
        std::uint8_t pocketed = 0;       ///< Empties in their pockets, up to kPockets
    };

    /// A look a guest gives something they walk on past, their head turned towards it a moment.
    struct Glance
    {
        entt::entity at = entt::null; ///< The path item looked at: jumping fountains
        Seconds left { };
    };

    /// What brings guests to the park, in the registry's context, kept up from the park as it runs. Guests
    /// come in at the plain weather rate when none is there.
    struct Draw
    {
        Money entrance_fee { };
        std::uint32_t pool_squares = 0;  ///< Squares of pool water
        std::optional<float> rating { }; ///< The park's, 0..1; none before it has one
        Money guest_cash { };            ///< What guests bring in on average; none for the tuning's
        Money guest_bank { };            ///< What cash machines can give them on average; none for the tuning's
    };

    /// Room in the park, in the registry's context, made by open rides that are not simulated yet (water
    /// slides, lazy rivers, rides with no timing): guests still come for them.
    struct RoomElsewhere
    {
        std::uint32_t guests = 0;
    };

    struct Wallet
    {
        Money cash { };
        Money bank { };      ///< What cash machines can still give them
        Money withdrawn { }; ///< What cash machines have given them
        /// What they have paid the park in all, and for each kind of thing; toilets and first aid count in all only.
        Money spent { };
        Money spent_entry { };
        Money spent_rides { };
        Money spent_food { };
        Money spent_drink { };
        Money spent_souvenirs { };
        bool needs_cash = false; ///< Could not afford something since they last drew cash
    };

    /// `v` kept within 0..1.
    [[nodiscard]] float clamp01(float v);

    /// How much a guest must wish for a swim to go for one: more the cooler the day, less the hotter.
    [[nodiscard]] float swim_wish_needed(float temperature);

    /// Most a guest pays for `ware`: its item's worth for the weather, more in the cold for a hot drink and in
    /// the heat for an ice cream; `flat` where that is not known.
    [[nodiscard]] Money worth_of(const Ware& ware, Money flat, float temperature);

    /// The guest pays the park `price`, counted in all and under `on` (none: in all only), whether they
    /// could. The rest of their party makes up what they are short of, the richest first. The park takes it in
    /// on `line` of its Receipts.
    bool pay(entt::registry& r, entt::entity guest, Money price, Money Wallet::* on, park::LedgerLine line);

    /// The next tile for `who` wandering at `at`, not back where they came from unless it is the only way.
    [[nodiscard]] NodeId wander(const PathNetwork& paths, NodeId at, NodeId came, Dice& dice, Walker who, const staff::Patrol* patrol = nullptr);

    /// A lane near `was`, so walkers drift across the path rather than zigzag from tile to tile. A party
    /// member keeps `slot` off their party's middle, and the middle drifts so the whole ring stays on the path.
    [[nodiscard]] glm::vec2 new_lane(Dice& dice, glm::vec2 was, glm::vec2 slot = { });

    /// How far a walker on `s` goes from tile to tile, lane to lane.
    [[nodiscard]] float step_length(const PathNetwork& paths, const Stride& s);

    /// Takes a step just begun `left` world units along, what the tick's walk had left past the tile, so
    /// walkers keep their pace through tiles.
    void walk_on(const PathNetwork& paths, Stride& s, float left);

    /// Walkers turn half way towards the way they moved this tick, so a step aside every other tick does not
    /// swing them to and fro, and no faster than turn_rate.
    void face_moved(Pose& pose, float dt);

    /// Whether a guest is still eating what they bought.
    [[nodiscard]] bool eating(const entt::registry& r, entt::entity guest);

    /// Whether a guest is still eating or drinking what they bought.
    [[nodiscard]] bool munching(const entt::registry& r, entt::entity guest);

    /// Frees a guest's hand of the empty in it: into a pocket, or onto path tile `at` when both are full.
    /// Their carried rubbish is gone once nothing is left of it.
    void free_hand(entt::registry& r, entt::entity guest, Carried& carried, NodeId at);

    /// A guest is sick on the path tile `at`, and feels better for it. Returns the mess.
    entt::entity be_sick(entt::registry& r, entt::entity guest, NodeId at);

    /// A guest's age group; adults' for one without a Visitor.
    [[nodiscard]] AgeGroup age_of(const entt::registry& r, entt::entity guest);

    /// A guest's taste in rides; any ride for one without a Taste.
    [[nodiscard]] Taste taste_of(const entt::registry& r, entt::entity guest);

    /// The time of day the guests feel, from the Daylight in the registry's context.
    [[nodiscard]] float time_of_day(const entt::registry& r);

    /// The weather the guests feel, from the registry's context.
    [[nodiscard]] Weather weather_in(const entt::registry& r);

    /// How far right of a line's middle a guest stands: to the other side from `ahead`, the one before them,
    /// so a line of people fills its width two abreast.
    [[nodiscard]] float side_after(random::RandomState& dice, float ahead);

    /// One tick of the guests: needs and moods, walking, followers keeping to their leader, spacing out on the
    /// paths and in queues, and arriving where they headed. Times each phase on `phase`.
    void step_guests(SimWorld& world, std::optional<Workers&> workers, const Weather& weather, bool night, std::optional<profiling::Scope>& phase);

    /// One tick at the park's gates, in `weather`: guests outside with nowhere to go walk to the gate to come in,
    /// or off to the nearest spawn point; new parties come, families by day and mostly teens after dark, at a
    /// spawn point to walk to the gate, or at the gate itself. Run as part of the "Tidy" phase.
    void step_gates(SimWorld& world, const Weather& weather);

    /// The gait a guest of `age`, feeling as `mood` and `needs` have them, goes on in from a tile: worn out,
    /// sick or bursting they show it, and otherwise now and then they run, children most and the fresher the
    /// likelier. Drawn with `dice`.
    [[nodiscard]] Gait pick_gait(AgeGroup age, const Mood& mood, const Needs& needs, random::RandomState& dice);

    /// The ground covered in a second by a guest whose own walk is `walk`, with `energy` left, in `gait`.
    [[nodiscard]] Pace gait_pace(Pace walk, Share energy, Gait gait);

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

    /// Gives guest `guest` what it needs to walk, standing on path node `at` with nothing to do. Its dice are
    /// seeded from the world's seed and its id, so the same park always plays out the same way.
    void start_walking(SimWorld& world, entt::entity guest, NodeId at);

    /// Brings a whole party in on path node `at`, of a kind the park's mix picks, as the gate does: a lone guest
    /// or a party, each walking with nothing to do yet. Returns its members, the leader first.
    std::vector<entt::entity> admit_party(SimWorld& world, random::RandomState& dice, NodeId at);

    /// Brings in `who` (each an age, and whether female) on path node `at` as one party, the first its leader,
    /// with tastes from `band`. Returns its members.
    std::vector<entt::entity> admit_guests(SimWorld& world, random::RandomState& dice, NodeId at, std::span<const std::pair<AgeGroup, bool>> who, const tuning::TasteBand& band);

} // namespace rct3::simulation
