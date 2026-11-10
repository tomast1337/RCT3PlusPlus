// SPDX-License-Identifier: GPL-3.0-or-later
//
// Rides: rides that load their riders, run and let them off, flat rides and coasters, with their stations,
// trains, boats and the settings for when they leave, and the step that runs their queues, riders and trains
// through a tick. The components here, and that step.
#pragma once

#include "math/glm.hpp"
#include "profiling/profiler.hpp"
#include "simulation/boat.hpp"
#include "simulation/coaster.hpp"
#include "simulation/crash.hpp"
#include "simulation/entt.hpp"
#include "simulation/path_network.hpp"
#include "simulation/units.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace rct3::simulation
{

    struct SimWorld;
    struct Weather;

    /// Where a ride without trains is in its cycle. It loads standing still, starts, runs its loops, stops, and
    /// lets its riders off as it goes back to loading.
    enum class RidePhase : std::uint8_t
    {
        Loading,
        Starting,
        Running,
        Stopping,
    };

    /// A station of a ride after its first, on a track whose trains stop at each: guests queue, board and get off
    /// there as at the first.
    struct Stop
    {
        NodeId entrance = kNoNode;
        std::vector<NodeId> line { };        ///< As CycleRide::line
        NodeId exit = kNoNode;               ///< Where riders get off; none lets no one off here
        std::vector<entt::entity> queue { }; ///< Guests waiting, first in line first
        Seconds loading { };                 ///< The train standing here has been loading
        static constexpr std::size_t kNone = std::numeric_limits<std::size_t>::max();
        std::size_t saved = kNone;           ///< Its place among the saved ride's stations; kNone till it has doors
        std::vector<glm::vec3> platform { }; ///< Points along its station track
    };

    struct CycleRide
    {
        std::uint32_t capacity = 8; ///< Guests a cycle
        std::uint32_t room = 0;     ///< Guests the ride makes room for in the park; 0 when its data does not say
        /// How long each part of a cycle lasts, in seconds: starting, one loop of running, and stopping. A
        /// ride timed by a whole lap runs one loop of it.
        Seconds start { };
        Seconds run { };
        Seconds stop { };
        std::uint32_t loops = 1;
        RidePhase phase = RidePhase::Loading;
        Seconds in_phase { };                ///< Into the phase; loading counts in `loading` instead
        std::uint32_t loop = 0;              ///< Loops of running done this cycle
        Share excitement { 0.5f };           ///< How much a ride cheers its riders
        Share intensity { 0.3f };            ///< How much it shakes them
        Share nausea { 0.3f };               ///< How sick it makes them
        Share cover { };                     ///< How much of it is under cover
        bool splash = false;                 ///< Rides through water, splashing its riders
        std::vector<entt::entity> queue { }; ///< Guests waiting, first in line first
        /// The queue line's tiles from the entrance back to where it meets the paths; empty when guests walk
        /// straight in from a path.
        std::vector<NodeId> line { };
        NodeId exit = kNoNode;                ///< Where riders get off; the attraction's entrance when kNoNode
        std::vector<entt::entity> riders { }; ///< Guests on the ride now
        Seconds loading { };                  ///< The train at the start, or the loading ride, has been loading
        Seconds since_departure { -1 };       ///< Since a train or go last left; -1 before the first
        float per_hour = 0;                   ///< Riders an hour, smoothed over the last departures; 0 before two
        bool synced = false;                  ///< Its train is ready and waits for those at the stations beside it
        bool released = false;                ///< Those are ready too: it leaves with them
        /// Its stations after the first (whose queue, line and exit are those above), in the order its trains
        /// come to them; empty for a ride with one
        std::vector<Stop> stops { };
    };

    /// How full a train must be to leave: Departure::wait_for, in the order the operating panel lists them.
    enum class LoadWait : std::uint8_t
    {
        Full,
        ThreeQuarters,
        Half,
        Quarter,
        Any, ///< At least one rider
    };

    /// When a ride's train, or its next ride, leaves the station: the operating panel's settings. A ride
    /// without one leaves as soon as anyone has boarded.
    struct Departure
    {
        LoadWait wait_for = LoadWait::Any;
        std::int32_t min_wait = -1;    ///< Seconds it waits before it looks at its load; -1 none
        std::int32_t max_wait = -1;    ///< Seconds after which it leaves whatever its load, even empty; -1 none
        bool leave_on_arrival = false; ///< Leaves, after its least wait, once the next train is held up behind it
        bool sync = false;             ///< Waits, ready, to leave with the trains at the stations beside it that do the same
    };

    /// The rides with a station on a tile beside one of this ride's, on the same level.
    struct StationNeighbours
    {
        std::vector<entt::entity> rides { };
    };

    /// The trains of a ride whose trains go round a track, each a Coaster of its own on the same track.
    /// The one standing at the start loads; the others run, or wait behind the train in front.
    /// The boats of a ride whose boats leave their dock for open water; they sail while its cycle runs, and it
    /// lets its riders off once every boat is back. The water they float on is a WaterTiles in the registry's
    /// context.
    struct Boats
    {
        std::vector<Boat> boats { };
        Dock dock { };
        bool roams = true;                ///< Hired boats roam; transport boats go from dock to dock
        std::vector<glm::vec2> calls { }; ///< The ride's other docks, in the order its boats call at them
        ovl::BoatHandling handling = kMostBoats;
        std::optional<float> top { }; ///< m/s its boats go at most, as the ride is set; the tuning's when none
        bool bumps = false;           ///< Its boats bounce off each other rather than keep clear
        std::size_t launched = 0;     ///< Of its boats, how many have cast off since its cycle began
    };

    struct Trains
    {
        std::vector<Coaster> trains;
        std::vector<std::vector<entt::entity>> riders; ///< By train
        LapRecorder lap { };                           ///< Of the first train's lap under way
        /// The first train's last lap, for the ride's graphs, till the park takes it.
        std::optional<save::model::RideGraphs> lap_done { };
    };

    /// A ride whose trains have crashed, and how long since the last did. A crashed train's cars fly off as
    /// wrecks, and the train is off the track till they are gone and the ride is opened again.
    struct Wrecks
    {
        std::vector<std::vector<Wreck>> trains { }; ///< By train; empty for one not crashed
        Seconds since { -1 };                       ///< Since the last crash; -1 once the park has forgotten it
        std::uint32_t crashes = 0;                  ///< Since the run began
        /// Dangerous happenings for the awards to count, since the run began: each crash of an open ride, and
        /// each car of it with riders.
        std::uint32_t dangers = 0;
        std::uint32_t injured = 0; ///< Riders hurt in its crashes since the run began
    };

    /// Keeps a train this far behind the back of the one in front, metres; our own figure.
    inline constexpr float kBlockGap = 4.0f;

    /// The clear track ahead of train `t` of `trains`, up to kBlockGap short of the back of the nearest
    /// train in front; endless for a lone train.
    [[nodiscard]] float room_ahead(std::span<const Coaster> trains, std::size_t t);

    /// Whether a train (or a ride's next go) with `riders` of `capacity` seats aboard leaves after `waited`
    /// seconds of loading, as `departure` says: never before its least wait; then once it has its load, or
    /// whatever its load (even none) once its longest wait is up.
    [[nodiscard]] bool departs(const Departure& departure, std::size_t riders, std::size_t capacity, float waited);

    /// How long a whole cycle of `ride` lasts: starting, its loops of running, and stopping.
    [[nodiscard]] float cycle_seconds(const CycleRide& ride);
    /// How far into its cycle `ride` is; nothing while it loads.
    [[nodiscard]] std::optional<float> seconds_into_cycle(const CycleRide& ride);
    /// Moves a ride that has left on by `dt` seconds through starting, running and stopping. True when it has
    /// just stopped, back to loading, and its riders get off; a `held` ride (its restraints or doors stuck shut)
    /// stays stopped with its riders aboard till it is not.
    bool advance_cycle(CycleRide& ride, float dt, bool held);

    /// One tick of the rides, in entity order: queues shuffle forward, riders get off, the next ones board, and
    /// trains and boats run. `weather` and `night` are the tick's, as the riders feel them. Times the phase on
    /// `phase`.
    void step_rides(SimWorld& world, const Weather& weather, bool night, std::optional<profiling::Scope>& phase);

} // namespace rct3::simulation
