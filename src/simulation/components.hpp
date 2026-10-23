// SPDX-License-Identifier: GPL-3.0-or-later
//
// The pieces the simulation's entities are made of. Plain values with no behaviour, so systems can read and
// write them in parallel and the park file can store them field by field.
//
// A guest is an entity with a Visitor, a Pose, Needs, a Mood and a Wallet; one the simulation moves also has
// an Errand, a Stride and Dice. Guests who go round together are Members of a Party, an entity of its own.
// An attraction is an entity with an Attraction and either a Shop or a
// CycleRide: a ride that loads its riders, runs for a while and lets them off, flat ride or coaster.
//
// Levels are fractions: needs run from 0 (satisfied) to 1 (desperate); mood values from 0 to 1. Times are
// simulation ticks (see simulation/systems.hpp).
#pragma once

#include "math/glm.hpp"
#include "random/generator.hpp"
#include "simulation/coaster.hpp"
#include "simulation/entt.hpp"
#include "simulation/path_network.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rct3::simulation
{

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

    /// Where an entity stands in the world.
    struct Pose
    {
        glm::vec3 position { 0.0f };
        float heading = 0;      ///< Radians round the vertical, 0 along +Z
        glm::vec3 was { 0.0f }; ///< Position when the last step began, to draw the entity between steps
        float was_heading = 0;
        float walked = 0; ///< Ground covered up to `was`, across, for the feet to keep step with
    };

    struct Needs
    {
        float hunger = 0;
        float thirst = 0;
        float bladder = 0;
    };

    struct Mood
    {
        float happiness = 0.5f;
        float energy = 0.5f;
        float nausea = 0;
        float tolerance = 0.5f; ///< How well the guest's stomach takes rides, 0..1: the higher, the wilder the rides they go on and the slower they sicken
        float wet = 0;          ///< From dry (0) to soaked (1)
        float heat = 0;         ///< From cool (0) to sweltering (1), out in the hot sun
    };

    /// Parties at the rides so far, in the registry's context: those held back from a load to ride together
    /// on the next, and those too many for one load, split over two.
    struct PartyBoarding
    {
        std::uint32_t held = 0;
        std::uint32_t split = 0;
    };

    /// The time of day the guests feel, in the registry's context; midday when none is there.
    struct Daylight
    {
        float time = 0.5f; ///< Fraction of a day from midnight
    };

    /// Where guests come into the park and leave it.
    struct ParkGate
    {
        NodeId node = kNoNode;
    };

    /// A guest who has gone home. Kept, without what made them move, for whoever still shows them.
    struct Left
    {
    };

    /// The kinds of party that come in at the gate. Children only ever come with an adult.
    // shortcut: no party of the player's own made guests (custom peeps), as players cannot make guests yet;
    // add a kind here when they can.
    enum class PartyKind : std::uint8_t
    {
        LoneTeen,
        LoneAdult,
        TeenCouple,  ///< A boy and a girl
        AdultCouple, ///< A man and a woman
        TeenGang,    ///< Teens, two or more
        AdultParty,  ///< Adults, two or more
        Family,      ///< One or two parents with their children
        DayTrip,     ///< One adult taking children out
    };
    inline constexpr std::size_t kPartyKinds = 8;

    /// The park's own mix of parties, in the registry's context, as a scenario sets it: how often each
    /// PartyKind comes against the others, and the share of families with one parent. The tuning's mix when
    /// none is there.
    struct PartyMix
    {
        std::array<float, kPartyKinds> weights { };
        float single_parent = 0;
    };

    /// Guests in and out of the gate so far, in the registry's context, by AgeGroup.
    struct Turnstile
    {
        std::array<std::uint32_t, 3> in { };
        std::array<std::uint32_t, 3> out { };
    };

    /// The intensity of rides a guest likes, 0..1 as rides are rated; any when they have none.
    struct Taste
    {
        float least = 0;
        float most = 1;
    };

    /// A guest carrying an umbrella, up while it rains.
    struct Umbrella
    {
    };

    /// The weather the guests feel, in the registry's context; fair and mild when none is there.
    struct Weather
    {
        bool rain = false;
        bool storm = false;
        bool sunny = false;     ///< Sun out, beating down on those not in the shade
        float temperature = 20; ///< Degrees Celsius
    };

    /// What brings guests to the park, in the registry's context, kept up from the park as it runs. Guests
    /// come in at the plain weather rate when none is there.
    struct Draw
    {
        std::int64_t entrance_fee = 0;   ///< Hundredths of the currency unit
        std::uint32_t pool_squares = 0;  ///< Squares of pool water
        std::optional<float> rating { }; ///< The park's, 0..1; none before it has one
    };

    /// Room in the park, in the registry's context, made by open rides that are not simulated yet (water
    /// slides, lazy rivers, rides with no timing): guests still come for them.
    struct RoomElsewhere
    {
        std::uint32_t guests = 0;
    };

    struct Wallet
    {
        std::int64_t cash = 0; ///< Hundredths of the currency unit
    };

    // ----- guests on the move -----

    /// What a guest is doing.
    enum class ErrandKind : std::uint8_t
    {
        Wandering, ///< Walking the paths with nowhere in mind
        Heading,   ///< Walking to `target`
        Queueing,  ///< In the queue of the ride `target`
        Riding,    ///< On the ride `target`
        Following, ///< Walking beside their party's leader, who chooses where to go
        Waiting,   ///< For the rest of their party to come off a ride, walking to its exit first
    };

    struct Errand
    {
        ErrandKind kind = ErrandKind::Wandering;
        entt::entity target = entt::null; ///< The attraction, when heading for one, queueing or riding
        std::uint32_t rest = 0;           ///< Ticks until the guest next considers what to do
        float queued = 0;                 ///< Seconds in the queue so far, while queueing
    };

    /// What a guest thinks, numbered as saved parks number their guests' thoughts, so the game's language
    /// files hold the words for each.
    enum class ThoughtKind : std::int32_t
    {
        Tired = 2,
        Litter = 3,
        Hungry = 4,
        Thirsty = 5,
        Toilet = 12,
        TooTame = 33,
        RainKeepsMeOff = 32,
        Vandalism = 14,
        Vomit = 15,
        Sick = 20,
        VerySick = 21,
        Broke = 24,
        CantAfford = 25,
        QueueingForAges = 31,
        TooIntense = 34,
        Enjoyed = 42,
        GoHome = 19,
        Raining = 93,
        Thunder = 94,
        Cold = 95,
        Hot = 96,
        GaveUpQueue = 131,
        QueueTooLong = 201, ///< Put off a ride by its queue, before joining
        Soaked = 191,
    };

    struct Thought
    {
        ThoughtKind kind = ThoughtKind::Tired;
        entt::entity about = entt::null; ///< The attraction thought about, if any
        float time = 0;                  ///< Seconds into the run
        std::uint32_t variant = 0;       ///< Picks which of the ways to say it
    };

    /// A guest's recent thoughts, newest first.
    struct Thoughts
    {
        std::vector<Thought> recent;
        std::uint32_t had = 0; ///< Thoughts had so far, for the park to tell new ones apart
    };

    /// Guests going round the park together, such as a family. The leader chooses where to go for all of
    /// them, by the most pressing needs among them; the others walk beside. At a ride some may stay out and
    /// wait by its exit for the rest. Now and then they stop and stand in a ring for a while.
    struct Party
    {
        entt::entity leader = entt::null;
        std::vector<entt::entity> members { }; ///< The leader among them
        std::uint32_t huddle = 0;              ///< Ticks left standing together in a ring, chatting
    };

    struct Member
    {
        entt::entity party = entt::null;
    };

    /// Where a guest is on the paths: on the way from node `from` to node `to`, `progress` of the way along.
    struct Stride
    {
        NodeId from = kNoNode;
        NodeId to = kNoNode;
        float progress = 0;
        bool fresh = false; ///< Came to `from` this tick
        float speed = 1.3f; ///< World units a second
        /// How far off the tiles' middles, along x and z, the guest walks at `from` and at `to`, so people
        /// spread over the path instead of keeping to one line.
        glm::vec2 lane_from { 0.0f }, lane_to { 0.0f };
        float along = 0;               ///< While queueing, tiles from the front of the line
        float side = 0;                ///< While queueing, world units right of the line's middle
        std::vector<NodeId> route { }; ///< Nodes still to reach after `to`, when heading somewhere
    };

    /// The guest's own random numbers, so each guest's choices do not depend on the order guests are handled in.
    struct Dice
    {
        random::RandomState state;
    };

    // ----- attractions -----

    enum class AttractionKind : std::uint8_t
    {
        Shop,
        Ride,
    };

    struct Attraction
    {
        AttractionKind kind = AttractionKind::Shop;
        std::string name;
        NodeId entrance = kNoNode; ///< The path node guests go to for it
        std::int64_t price = 0;    ///< Hundredths of the currency unit
        bool open = true;
        bool testing = false;    ///< A ride run empty to try it out, guests kept off
        std::int64_t income = 0; ///< Hundredths, taken so far
        std::uint32_t customers = 0;
        std::uint64_t saved = 0; ///< The saved park's own id of it; 0 when not from a save
    };

    /// What a shop sells, or what a facility does for its guests.
    enum class Product : std::uint8_t
    {
        Drink,
        Food,
        Toilet,      ///< Relieves the bladder
        CashMachine, ///< Gives the guest more cash
        FirstAid,    ///< Settles a sick guest's stomach
        Umbrella,    ///< Keeps the rain off
    };

    /// Whether a shop's food or drink is cold (ice cream, a slush), hot (coffee, soup) or neither.
    enum class Serve : std::uint8_t
    {
        Any,
        Cold, ///< Cools a guest down; wanted most on hot days
        Hot,  ///< Wanted most on cold days
    };

    struct Shop
    {
        Product product = Product::Drink;
        Serve serve = Serve::Any;
    };

    /// Where a ride without trains is in its cycle. It loads standing still, starts, runs its loops, stops, and
    /// lets its riders off as it goes back to loading.
    enum class RidePhase : std::uint8_t
    {
        Loading,
        Starting,
        Running,
        Stopping,
    };

    struct CycleRide
    {
        std::uint32_t capacity = 8; ///< Guests a cycle
        std::uint32_t room = 0;     ///< Guests the ride makes room for in the park; 0 when its data does not say
        /// How long each part of a cycle lasts, in seconds: starting, one loop of running, and stopping. A
        /// ride timed by a whole lap runs one loop of it.
        float start_seconds = 0;
        float run_seconds = 0;
        float stop_seconds = 0;
        std::uint32_t loops = 1;
        RidePhase phase = RidePhase::Loading;
        float phase_seconds = 0;             ///< Seconds into the phase; loading counts in `loading` instead
        std::uint32_t loop = 0;              ///< Loops of running done this cycle
        float excitement = 0.5f;             ///< How much a ride cheers its riders, 0..1
        float intensity = 0.3f;              ///< How much it shakes them, 0..1
        float nausea = 0.3f;                 ///< How sick it makes them, 0..1
        float cover = 0;                     ///< How much of it is under cover, 0..1
        bool splash = false;                 ///< Rides through water, splashing its riders
        std::vector<entt::entity> queue { }; ///< Guests waiting, first in line first
        /// The queue line's tiles from the entrance back to where it meets the paths; empty when guests walk
        /// straight in from a path.
        std::vector<NodeId> line { };
        NodeId exit = kNoNode;                ///< Where riders get off; the attraction's entrance when kNoNode
        std::vector<entt::entity> riders { }; ///< Guests on the ride now
        float loading = 0;                    ///< Seconds the train at the start, or the loading ride, has been loading
        float since_departure = -1;           ///< Seconds since a train or go last left; -1 before the first
        float per_hour = 0;                   ///< Riders an hour, smoothed over the last departures; 0 before two
    };

    /// The ways riders walk on and off a ride, in the park's space, worked out from the ride's models where it
    /// is drawn. Seats are counted as a load fills them: a ride's riders, or a train's. A ride without one lets
    /// its riders on and off where they stand.
    struct RideWalks
    {
        glm::vec3 ticket { 0.0f }; ///< Where a guest stands at the entrance to hand over their ticket
        float ticket_heading = 0;  ///< The way they face there, as Pose::heading
        /// By seat: from the ticket spot to the seat, and from the seat out past the exit.
        std::vector<std::vector<glm::vec3>> on { };
        std::vector<std::vector<glm::vec3>> off { };
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
    };

    /// The trains of a ride whose trains go round a track, each a Coaster of its own on the same track.
    /// The one standing at the start loads; the others run, or wait behind the train in front.
    struct Trains
    {
        std::vector<Coaster> trains;
        std::vector<std::vector<entt::entity>> riders; ///< By train
    };

    // ----- litter, path items and staff -----

    /// A piece of litter lying on a path tile until a handyman sweeps it up.
    struct Litter
    {
        NodeId node = kNoNode;
        /// What it looks like, numbered as saved parks number it (0..5).
        /// shortcut: which saved number is a cup or a wrapper is not known, so ours are 0 for food and 1 for
        /// drink; upgrade when litter is drawn.
        std::uint8_t kind = 0;
        float age = 0;      ///< Seconds it has lain
        bool vomit = false; ///< Left by a sick guest
    };

    /// What a guest is left holding once they have eaten or drunk what they bought.
    struct Rubbish
    {
        std::uint8_t kind = 0; ///< As Litter::kind, once dropped
        float held = 0;        ///< Seconds since they bought it, eating or drinking first
    };

    enum class PathItemKind : std::uint8_t
    {
        Bin,
        Bench,
        Sign,
        Sight,     ///< Something to stop and watch: a fountain, an effect, an animatronic
        PhotoSpot, ///< A view to stop at, where a player's guest has their photo taken
    };

    /// Something standing on a path tile that guests use and vandals break.
    struct PathItem
    {
        NodeId node = kNoNode;
        PathItemKind kind = PathItemKind::Bin;
        bool broken = false; ///< Of no use until a handyman or mechanic mends it
    };

    /// A guest stopped on the path a while: sitting on a bench, or watching a sight.
    struct Linger
    {
        PathItemKind at = PathItemKind::Bench;
        float left = 0; ///< Seconds
    };

    /// How a guest shows what they feel, standing a moment where they are.
    enum class GestureKind : std::uint8_t
    {
        Cheer,   ///< Jumping for joy off a ride they loved
        Laugh,   ///< Off a ride they loved
        Nod,     ///< Off a ride they liked well enough
        Grumble, ///< Shaking their head at a ride too tame for them
        Dizzy,   ///< Reeling off a ride too wild for them
        Retch,   ///< Being sick
    };

    struct Gesture
    {
        GestureKind kind = GestureKind::Nod;
        float left = 0; ///< Seconds
    };

    /// A rider walking on to their seat from the ticket spot, or off it to the paths: still riding, but on
    /// their feet. `points` are walked in turn; the first, on the way on, is the ticket spot, where they stand
    /// for `pause` seconds first.
    struct RideWalk
    {
        std::vector<glm::vec3> points;
        std::size_t next = 0; ///< The point walked to now
        float pause = 0;
        bool off = false;
        std::optional<GestureKind> gesture { }; ///< Shown once off, on the paths
    };

    /// What a bin holds, in pieces of rubbish.
    struct Bin
    {
        float capacity = 0;
        float contents = 0;
    };

    enum class StaffRole : std::uint8_t
    {
        Handyman, ///< Sweeps, empties bins and mends what is broken
        Mechanic, ///< Mends what is broken
        Guard,    ///< Keeps vandals off the paths near them
        Other,    ///< Walks the paths, nothing more yet
        Vendor,   ///< Sells at a stall or a ride's entrance, standing at their post off the paths
    };

    struct Staff
    {
        StaffRole role = StaffRole::Other;
        float busy = 0;                 ///< Seconds left at the job in hand
        entt::entity ride = entt::null; ///< The ride a mechanic is going to, or working on, to repair or inspect
        std::int64_t wage = 0;          ///< Hundredths of the currency unit a game hour
        std::int32_t wage_level = 2;    ///< 0 to 2, what `wage` follows (staff_care.hpp)
        std::int32_t training = 3;      ///< 0 untrained to 3; new staff come fully trained until hiring costs money
        std::int32_t warnings = 0;
        float happiness = 1;  ///< 0 to 1
        float laziness = 0;   ///< 0 to 1: how much longer each job takes, and how often they stop to rest
        float load = 0;       ///< Share of the recent past spent at work, 0 to 1
        bool resting = false; ///< `busy` is a rest, not a job
    };

    /// How a ride has broken down, numbered as saved parks number breakdowns.
    enum class Breakdown : std::int32_t
    {
        None = 0,
        EmergencyStop = 1,        ///< A coaster's trains halt where they are
        RestraintsFailToOpen = 2, ///< Riders cannot get off
        RestraintsFailToClose = 3,
        DoorsFailToOpen = 4, ///< Riders cannot get off
        DoorsFailToClose = 5,
        TrainStuckLeavingStation = 6,
        StationBrakesFail = 7,
        ControlFailure = 8,
    };

    /// The tiles a staff member keeps to. They wander and work only on these, and walk back to them when
    /// they find themselves outside, after a job elsewhere. Staff without one have the whole park.
    struct Patrol
    {
        std::vector<glm::ivec2> tiles; ///< (col, row), any level
        std::uint32_t zone = 0;        ///< A shared zone: painting one on it paints all on it; 0 none

        [[nodiscard]] bool covers(const PathTileSpot& at) const
        {
            return std::ranges::contains(tiles, glm::ivec2(at.col, at.row));
        }
    };

    /// A ride's wear. Each run wears it a little, and the less reliable it is the likelier a run ends in a
    /// breakdown, which closes it until a mechanic repairs it. Inspections win some reliability back.
    struct Upkeep
    {
        float reliability = 1; ///< 0..1
        bool broken = false;
        float since_inspection = 0;         ///< Seconds
        float inspect_every = 600;          ///< Seconds; negative never
        entt::entity mechanic = entt::null; ///< Sent to it, or at work on it
        bool called = false;                ///< The player asked for a mechanic, who inspects it as if due
        std::uint32_t breakdowns = 0;       ///< Since the run began
        Breakdown kind = Breakdown::None;   ///< The breakdown going on
        Breakdown last = Breakdown::None;   ///< The latest one, going on or fixed
        /// Downtime: each window is sampled a set number of times, and counts the samples the ride was down
        /// in. Newest first; the oldest drops off as a new window starts.
        std::array<std::uint8_t, 8> downtime { };
        std::uint8_t samples = 0; ///< Taken in the newest window
        float sample_clock = 0;   ///< Seconds since the last sample

        /// Share of the samples in all windows taken while down, 0..1.
        [[nodiscard]] float downtime_share() const;
    };

    /// What the park has paid its staff, kept in the registry's context.
    struct Payroll
    {
        std::int64_t paid = 0; ///< Hundredths
        double owed = 0;       ///< Hundredths earned and not yet paid, under one
    };

    /// The park's run of vandalism, kept in the registry's context.
    /// A photo taken of a player's guest, with their party, at a photo spot.
    struct Photo
    {
        entt::entity guest = entt::null; ///< The player's guest
        NodeId at = kNoNode;             ///< The spot's path tile
    };

    /// In the registry's context: photos taken since whoever shows the park last took them away to save.
    struct Photos
    {
        std::vector<Photo> taken;
    };

    struct Vandalism
    {
        std::uint32_t broken = 0;   ///< Things broken since the run began
        float recent = 0;           ///< Things broken lately, each fading away over a game hour or so
        float since_warning = -1;   ///< Seconds since the player was last told; -1 never
        std::uint32_t warnings = 0; ///< Times the player has been told, for the park to post as messages
    };

} // namespace rct3::simulation
