// SPDX-License-Identifier: GPL-3.0-or-later
//
// Swimming: pool complexes, their changing rooms and the guests who swim in them, the lazy rivers they float
// round and the slides they climb on foot. The components here, and the step that moves them.
#pragma once

#include "math/glm.hpp"
#include "park/pools/score.hpp"
#include "profiling/profiler.hpp"
#include "simulation/coaster.hpp"
#include "simulation/entt.hpp"
#include "simulation/units.hpp"

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rct3::simulation
{

    struct SimWorld;

    /// What swimmers do at a pool extra.
    enum class PoolUse : std::uint8_t
    {
        Stand,  ///< Stand by it a while
        Lounge, ///< Lie on a sun lounger
        Shower, ///< Stand under a shower
    };

    /// A pool extra swimmers go to, in the middle of its paved square.
    struct PoolSpot
    {
        glm::vec3 at { 0.0f };
        Radians heading { }; ///< The way it faces
        PoolUse use = PoolUse::Stand;
        std::uint8_t places = 1; ///< Swimmers it takes at once; more wait their turn beside it
    };

    /// A diving board of a pool complex, small on the pool's edge or high on a paved square: where swimmers
    /// step on, the boards they may dive from, and the way it faces.
    struct PoolBoard
    {
        glm::vec3 foot { 0.0f };       ///< On the paving: a high board's ladder foot, a small board's back
        Radians heading { };           ///< Out over the water, the way they dive
        std::vector<float> levels { }; ///< The boards' tops they may walk out on, lowest first, in world units; one for a small board
        float reach = 0;               ///< From the foot to the boards' end, the way it faces, in world units
        bool high = false;
    };

    /// A ladder on a pool's wall: swimmers climb out by it, or down it into the water.
    struct PoolLadder
    {
        glm::vec3 water { 0.0f }; ///< In the water at its foot, where those climbing out start and those climbing in end
        glm::vec3 deck { 0.0f };  ///< On the paving behind it, where those climbing out step off and those climbing in start
        Radians heading { };      ///< Facing it, out of the water
    };

    /// A pool's open water is marked in this many steps across a square, each way.
    inline constexpr std::size_t kOpenSteps = 16;

    /// A square of a pool complex swimmers may be on, and the squares they may go on to from it: where paving meets
    /// paving or the coping round water at the same height, water meets water, a slope or steps meet at their foot
    /// or top, or a ramp or a spiral staircase climbs from one to the other. A spiral staircase has one at its foot
    /// and one at its top.
    struct PoolWalk
    {
        struct Join
        {
            std::size_t to = 0;
            std::vector<glm::vec3> via { }; ///< Walked through on the way, from this square: where the two meet, or a ramp's or staircase's ends
        };
        glm::vec3 at { 0.0f }; ///< Its middle, at its paving's height or, for water, its water's surface
        bool water = false;    ///< Swum across; walked only along its coping
        bool end = false;      ///< Gone to but never through: a spa pool's, or a high diving board's
        std::vector<Join> joins { };
    };

    /// A pool complex: the squares its swimmers go to, each spot the middle of a square at its surface.
    struct SwimPool
    {
        std::vector<glm::vec3> water { };
        /// Of each square of `water`, where its water is open, clear of the paving and coping round it: kOpenSteps by
        /// kOpenSteps marks, row by row from its corner of least column and row.
        std::vector<std::bitset<kOpenSteps * kOpenSteps>> open { };
        std::vector<glm::vec3> paved { };
        std::vector<glm::vec3> spa { };   ///< Where swimmers sit in the spa pools, at the spa's water
        std::vector<PoolSpot> extras { }; ///< The paved squares with an extra on them, loungers and showers
        std::vector<glm::vec3> steps { }; ///< Its pool path stairs, at their top, which a pool ride's station may join as it may its paving
        std::vector<PoolBoard> boards { };
        std::vector<PoolLadder> ladders { };
        std::vector<PoolWalk> walks { };        ///< Its squares as swimmers go from one to the next
        Share dirt { };                         ///< From clean (0) to a health hazard (1); swimmers dirty it, its rooms' fluid cleans it
        rct3::park::pools::PoolTally tally { }; ///< What it has, for its excitement and what its ticket is worth
    };

    /// How dirty a pool looks, in the six steps the pool's window names, cleanest first.
    [[nodiscard]] std::size_t dirt_step(const SwimPool& pool);

    /// Whether `at` is over the open water of the square `square` of `pool`'s water, `tile` across.
    [[nodiscard]] bool open_water(const SwimPool& pool, std::size_t square, glm::vec3 at, float tile);

    /// Where something going straight across `pool` (its squares `tile` across) from `from` to `to` is stopped by
    /// its walls: once over its open water, at the last of it on the way; `to` if it never comes out of it.
    [[nodiscard]] glm::vec2 pool_stop(const SwimPool& pool, glm::vec2 from, glm::vec2 to, float tile);

    /// The walk of `pool` (its squares `tile` across) that `at` is on, the one nearest its height where squares are
    /// stacked; off them, the nearest whose middle is within kTuning.pools.platform squares across and two height
    /// units `level` high, as a slide's station is from the path beside its platform. None further off.
    [[nodiscard]] std::optional<std::size_t> pool_walk_at(const SwimPool& pool, glm::vec3 at, float tile, float level);

    /// Of each walk of `pool`, the one before it on the shortest way there from `from` over the joins, crossing no
    /// square that is only gone to; `from` for itself, none where it cannot be reached.
    [[nodiscard]] std::vector<std::optional<std::size_t>> pool_ways(const SwimPool& pool, std::size_t from);

    /// The cleaning fluids a changing room may keep its pool clean with, mildest first.
    enum class PoolFluid : std::uint8_t
    {
        Mild,
        Strong,
        Harsh, ///< Cleans fast, but stings swimmers' eyes in a pool with little dirt to clean
    };

    /// A changing room's cleaning fluid, on its PoolDoor: which, and what is left of it. Mechanics inspecting the
    /// room fill it up again, at the fluid's price.
    struct PoolTank
    {
        PoolFluid fluid = PoolFluid::Mild;
        Share left { 1 };
    };

    /// A changing room of a pool complex, on its Attraction, whose entrance is the path tile at its door.
    struct PoolDoor
    {
        entt::entity pool = entt::null; ///< Its SwimPool
        std::int32_t col = 0;           ///< Its tile
        std::int32_t row = 0;
        glm::vec3 room { 0.0f };     ///< The middle of its tile, at the pool's surface
        glm::vec3 poolside { 0.0f }; ///< The middle of the paved square it opens on
    };

    /// How far a swimmer is with a dive off one of the pool's boards.
    enum class DiveStep : std::uint8_t
    {
        None,    ///< Not diving; on the way to a board's foot when they have one
        Climb,   ///< Up a high board's ladder
        Walk,    ///< Out along the board to its end
        Jump,    ///< Springing off its end
        Fall,    ///< Down through the air
        Surface, ///< Back up after going under
    };

    /// How a swimmer crosses the water's edge: each way a motion of its own, played where they stand while it
    /// carries them over.
    enum class PoolEdge : std::uint8_t
    {
        None,
        DiveIn,    ///< Off the edge, head first
        ClimbOut,  ///< Up over the wall
        LadderOut, ///< Up a ladder
        LadderIn,  ///< Down a ladder
    };

    /// A guest at a pool, from paying at its changing room until they come out of it again.
    struct Swimmer
    {
        Seconds changing { };            ///< Still to spend changing, out of sight in the room
        bool changed = false;            ///< Into their swimwear
        Seconds left { };                ///< In the pool still; then they go back to change
        glm::vec3 goal { 0.0f };         ///< Where they swim or walk to next
        bool in_water = false;           ///< Swimming, or sitting in a spa pool; else on the paved squares round the water
        bool to_spa = false;             ///< The goal is a seat in a spa pool
        Seconds idle { };                ///< Floating, or standing, before going on
        std::int32_t spot = -1;          ///< The extra of the pool's they head for, wait at or use; -1 for none
        std::int32_t place = -1;         ///< Their place on it while they use it; -1 on the way or waiting
        PoolUse use = PoolUse::Stand;    ///< What they do there
        Seconds waited { };              ///< For a place on it so far
        Seconds stayed { };              ///< At their goal so far
        entt::entity slide = entt::null; ///< The pool slide they head for, to queue at its station
        /// The changing room of the complex whose slide they queue for or ride, to swim on in after
        entt::entity room = entt::null;
        std::int32_t board = -1;        ///< The pool's diving board they head for or dive off; -1 for none
        DiveStep dive = DiveStep::None; ///< How far they are with it
        float height = 0;               ///< The board's top they climb to and walk out on, in world units
        float fall = 0;                 ///< Their speed down, falling, in world units a second
        Seconds diving { };             ///< In this step so far
        std::int32_t ladder = -1;       ///< The pool's ladder they head for, to climb out or in by; -1 for none
        PoolEdge edge = PoolEdge::None; ///< Over the water's edge now
        glm::vec3 edge_from { 0.0f };   ///< Where they stand for it, the motion carrying them on from there
        glm::vec3 edge_to { 0.0f };     ///< Where the motion leaves them
        Seconds edging { };             ///< Into it so far
    };

    /// A swimmer's way to their goal over the squares of their pool joined to the one they are on; not kept in saves,
    /// they find it again.
    struct PoolRoute
    {
        struct Point
        {
            glm::vec3 at { 0.0f };
            bool water = false; ///< On the way there they cross a square of water, swimming or along its coping
        };
        glm::vec3 goal { 0.0f };    ///< The goal it was found for
        std::vector<Point> via { }; ///< The points still to go through before the goal, the next last
    };

    /// A lazy river: a channel with no trains round which swimmers float in rings, free on their complex's ticket.
    /// They get on and off at its stations, each joined to the complex whose paving or pool paths lie beside it.
    struct River
    {
        struct Station
        {
            float at = 0;                      ///< Along the course, the middle of the station, where riders get on and off
            std::vector<glm::vec3> points { }; ///< Of the course along it
            entt::entity pool = entt::null;    ///< The complex it joins (its SwimPool); none till one does
            glm::vec3 square { 0.0f };         ///< The square beside it that joins it, where swimmers come and go
        };
        Coaster course { };
        std::vector<Station> stations { };
    };

    /// A swimmer floating round a lazy river in a ring, on top of their Swimmer and riding Errand.
    struct Floating
    {
        entt::entity river = entt::null;
        float along = 0;     ///< Where they are along the course
        float travelled = 0; ///< Since they got on
        float stay = 0;      ///< How far they float before getting off at the next station on
        float lane = -1;     ///< Across the channel, -1 to 1
        Radians facing { };  ///< In their ring, from along the current
        float spin = 0;      ///< Radians a second their ring turns while they rest
        Pace paddle { };     ///< How fast they paddle, the way they face
        bool paddling = false;
    };

    /// A pool slide with no cars, as on an inflatable ship: swimmers climb it on foot from the water and slide down
    /// on their backs into it, free on their complex's ticket.
    struct ClimbSlide
    {
        struct Way
        {
            Coaster course { }; ///< From its foot in the water, up on foot, then down the slide
            float top = 0;      ///< Along the course, where the climb ends and the slide begins
        };
        std::vector<Way> ways { };
    };

    /// A swimmer on a ClimbSlide, on top of their Swimmer and riding Errand.
    struct Climbing
    {
        entt::entity slide = entt::null;
        std::uint8_t way = 0;
        float along = 0;
        Pace speed { }; ///< Along the way
    };

    /// The changing room of the pool complex whose water, paving or spa pool lies under `point`, if any.
    [[nodiscard]] std::optional<entt::entity> pool_door_at(const SimWorld& world, glm::vec3 point);

    /// Where a pool ride's station joins a pool complex.
    struct PoolJoin
    {
        entt::entity pool = entt::null; ///< The complex (its SwimPool)
        glm::vec3 square { 0.0f };      ///< The middle of the square beside the station that joins it, where guests come and go
    };

    /// The pool complex a pool ride's station joins, its track running through `station`: one whose paving,
    /// pool path or path stairs lie on a square along one of the station's sides, at its level, the nearest when
    /// several do, the track standing up to `raised` over it (a slide's over its platform's deck). Pool rides have no
    /// entrance or exit: their guests board from that square and get off onto it.
    [[nodiscard]] std::optional<PoolJoin> pool_beside(const SimWorld& world, std::span<const glm::vec3> station, float raised = 0);

    /// The pool complex (its SwimPool) a pool ride belongs to: a lazy river by its joined stations, a slide climbed
    /// on foot by its foot, a slide ending in a pool by that pool, and a slide with no entrance of its own going
    /// round back to its station by the complex its station joins. None for every other ride.
    [[nodiscard]] std::optional<entt::entity> pool_ride_pool(const SimWorld& world, entt::entity ride);

    /// Whether `person` wears their swimwear now: inside a pool complex, from changing into it till changing
    /// back, and held or falling out of one till they land; their clothes everywhere else.
    [[nodiscard]] bool in_swimwear(const entt::registry& r, entt::entity person);

    /// One tick of the pools: changing rooms use up their cleaning fluid, swimmers change, swim and dive, and
    /// riders float round lazy rivers and climb and slide down the slides climbed on foot. Times each phase on
    /// `phase`.
    void step_pools(SimWorld& world, std::optional<profiling::Scope>& phase);

} // namespace rct3::simulation
