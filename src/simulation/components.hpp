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
#include "simulation/boat.hpp"
#include "simulation/coaster.hpp"
#include "simulation/crash.hpp"
#include "simulation/entt.hpp"
#include "simulation/path_network.hpp"
#include "simulation/units.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
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

    /// A guest of honour a scenario's goal sent (see vip.hpp). They go home only once their visit is over or
    /// their patience has run out.
    struct Vip
    {
        std::uint64_t goal = 0;                 ///< The goal's condition, by its id
        std::uint8_t who = 0;                   ///< Which celebrity, as the goal numbers them
        Share patience { 1 };                   ///< Litter and breakdowns wear it down; at 0 they leave unhappy
        std::vector<entt::entity> coasters { }; ///< Different coasters they rode that were exciting enough
        bool fireworks = false;
        bool themed = false;
        float came = 0;          ///< Calendar time they came in
        Seconds look { };        ///< Until they next look round
        float happiness_sum = 0; ///< Over their looks, for their average
        std::uint32_t looks = 0;
        bool leaving = false;
        Seconds poll { };             ///< Until they next look for fans about them
        std::uint32_t fans = 0;       ///< Gathered round them for an autograph now
        std::uint32_t signatures = 0; ///< Autographs given since their last look round
    };

    /// Where an entity stands in the world.
    struct Pose
    {
        glm::vec3 position { 0.0f };
        Radians heading { };    ///< Round the vertical, 0 along +Z
        glm::vec3 was { 0.0f }; ///< Position when the last step began, to draw the entity between steps
        Radians was_heading { };
        Distance walked { }; ///< Ground covered up to `was`, across, for the feet to keep step with
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
        Share time { 0.5f }; ///< Of the day, from midnight
    };

    /// Where guests come into the park and leave it; none come in while the park is closed.
    struct ParkGate
    {
        NodeId node = kNoNode;
        bool closed = false;
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

    /// The path tiles under the park's entrances, in the registry's context: staff never step onto them, so they
    /// never walk out of the park.
    struct EntrancePaths
    {
        std::vector<NodeId> nodes;
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
        Chance single_parent { };
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

    /// The weather the guests feel, in the registry's context; fair and mild when none is there.
    struct Weather
    {
        bool rain = false;
        bool storm = false;
        bool sunny = false; ///< Sun out, beating down on those not in the shade
        Degrees temperature { 20 };
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

    // ----- guests on the move -----

    /// What a guest is doing.
    enum class ErrandKind : std::uint8_t
    {
        Wandering, ///< Walking the paths with nowhere in mind
        Heading,   ///< Walking to `target`
        Queueing,  ///< In the queue of the ride `target`
        Riding,    ///< On the ride `target`
        Following, ///< Walking beside their party's leader, who chooses where to go
        Waiting,   ///< For the rest of their party, on a tile near where those still busy are
        Buying,    ///< At the stall `target`, served for `rest` ticks while the rest of their party waits
        Swimming,  ///< At the pool whose changing room is `target`, changing or in the pool (see Swimmer)
    };

    struct Errand
    {
        ErrandKind kind = ErrandKind::Wandering;
        entt::entity target = entt::null; ///< The attraction, when heading for one, queueing or riding
        std::uint32_t rest = 0;           ///< Ticks until the guest next considers what to do
        Seconds queued { };               ///< In the queue so far, while queueing
        /// Of a ride with more than one station, the one queued at or boarded at (0 the first, k its k-th stop),
        /// and the one to get off at
        std::uint8_t station = 0;
        std::uint8_t off = 0;
        Seconds turn { }; ///< At a counter, how long their turn there lasts all told
    };

    /// What a guest thinks, numbered as saved parks number their guests' thoughts, so the game's language
    /// files hold the words for each.
    enum class ThoughtKind : std::int32_t
    {
        Lost = 1, ///< Cut off from their party
        Tired = 2,
        Litter = 3,
        ReallyTidy = 6,   ///< No litter or damage round them
        GreatScenery = 8, ///< Plenty of scenery round them
        JumpingFountainsGood = 189,
        NiceMusic = 29,     ///< About the loudspeaker they heard
        PhotoSpotGood = 85, ///< At a view with plenty of scenery round it
        Photographer = 88,  ///< Took a photo of the player's guest at a view
        Photographee = 89,  ///< The player's guest, photographed at one
        Hungry = 4,
        Thirsty = 5,
        Toilet = 12,
        TooTame = 33,
        RainKeepsMeOff = 32,
        Vandalism = 14,
        Vomit = 15,
        NotHungry = 10,        ///< Along at a food stall without wanting anything
        NotThirsty = 11,       ///< Along at a drink stall without wanting anything
        AlreadyGot = 22,       ///< Along at an umbrella stall with one already
        HaventFinished = 23,   ///< At a stall while still eating or drinking
        GoodValue = 26,        ///< Bought something well under what it is worth to them
        StingyToppings = 59,   ///< Bought something that came without any of the extras it takes
        MassesOfToppings = 60, ///< Bought something that came with every extra it takes
        NotPaying = 27,        ///< Put off by a price over what it is worth to them
        NotPayingToilet = 44,  ///< Put off a toilet by a price over what their need makes it worth
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
        Disaster = 62, ///< Saw a ride crash
        PickedUpHappy = 71,
        PickedUpNeutral = 72,
        PickedUpSad = 73,
        BeenInjured = 163,     ///< Thrown from a crashed ride, or knocked over by its blast
        PoolGood = 179,        ///< Out of a pool kept clean
        PoolBad = 180,         ///< Out of a dirty one
        PoolOvercleaned = 181, ///< Out of one cleaned with too harsh a fluid
        NoPools = 182,         ///< Wanting a swim in a park with no pool open
        TooColdForPools = 198,
        // A worker's (see staff_thought)
        StaffBored = 47,
        StaffTooBusy = 48,
        StaffTired = 49,
        StaffWantToQuit = 50,
        StaffMiserable = 51,
        StaffUnderpaid = 52,
        GreatEntertainer = 38,  ///< After watching a good show
        BoringEntertainer = 39, ///< After watching a poor one
        StaffHappy = 56,
        StaffMotivated = 57,
    };

    struct Thought
    {
        ThoughtKind kind = ThoughtKind::Tired;
        entt::entity about = entt::null; ///< The attraction thought about, if any
        Seconds time { };                ///< Into the run
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
        std::uint32_t huddle = 0;              ///< Ticks left standing together in a ring, chatting (once all are back, too)
        NodeId gather = kNoNode;               ///< The tile it waits on for those still busy, none of another party's
        std::vector<entt::entity> away { };    ///< Members out of it for now (held up, falling, thrown, sliding), back on landing
    };

    struct Member
    {
        entt::entity party = entt::null;
    };

    /// Where a guest is on the paths: on the way from node `from` to node `to`, `progress` of the way along.
    /// How a guest goes on foot: how fast, and how they look going.
    enum class Gait : std::uint8_t
    {
        Walk,
        Run,      ///< Full of beans, or catching up with their party
        Weary,    ///< Worn out, dragging their feet
        Queasy,   ///< Feeling sick
        Bursting, ///< Needing the toilet badly
    };

    struct Stride
    {
        NodeId from = kNoNode;
        NodeId to = kNoNode;
        Share progress { };
        bool fresh = false;  ///< Came to `from` this tick
        Pace speed { 1.3f }; ///< Their own walk, before their gait's pace (gait_pace)
        Gait gait = Gait::Walk;
        /// How far off the tiles' middles, along x and z, the guest walks at `from` and at `to`, so people
        /// spread over the path instead of keeping to one line.
        glm::vec2 lane_from { 0.0f }, lane_to { 0.0f };
        Tiles along { };               ///< While queueing, from the front of the line
        Distance side { };             ///< While queueing, right of the line's middle
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
        Pool, ///< A changing room of a pool, where guests pay to swim
    };

    struct Attraction
    {
        AttractionKind kind = AttractionKind::Shop;
        std::string name;
        NodeId entrance = kNoNode; ///< The path node guests go to for it
        Money price { };
        bool open = true;
        bool testing = false;   ///< A ride run empty to try it out, guests kept off
        bool off_theme = false; ///< Unlike the scenery round it, as the park last judged
        Money income { };       ///< Taken so far
        std::uint32_t customers = 0;
        std::uint64_t saved = 0; ///< The saved park's own id of it; 0 when not from a save
    };

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

    /// A pool complex: the squares its swimmers go to, each spot the middle of a square at its surface.
    struct SwimPool
    {
        std::vector<glm::vec3> water { };
        std::vector<glm::vec3> paved { };
        std::vector<glm::vec3> spa { };   ///< Where swimmers sit in the spa pools, at the spa's water
        std::vector<PoolSpot> extras { }; ///< The paved squares with an extra on them, loungers and showers
        Share dirt { };                   ///< From clean (0) to a health hazard (1); swimmers dirty it, its rooms' fluid cleans it
    };

    /// How dirty a pool looks, in the six steps the pool's window names, cleanest first.
    [[nodiscard]] std::size_t dirt_step(const SwimPool& pool);

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

    /// A guest at a pool, from paying at its changing room until they come out of it again.
    struct Swimmer
    {
        Seconds changing { };         ///< Still to spend changing, out of sight in the room
        bool changed = false;         ///< Into their swimwear
        Seconds left { };             ///< In the pool still; then they go back to change
        glm::vec3 goal { 0.0f };      ///< Where they swim or walk to next
        bool in_water = false;        ///< Swimming, or sitting in a spa pool; else on the paved squares round the water
        bool to_spa = false;          ///< The goal is a seat in a spa pool
        Seconds idle { };             ///< Floating, or standing, before going on
        std::int32_t spot = -1;       ///< The extra of the pool's they head for, wait at or use; -1 for none
        std::int32_t place = -1;      ///< Their place on it while they use it; -1 on the way or waiting
        PoolUse use = PoolUse::Stand; ///< What they do there
        Seconds waited { };           ///< For a place on it so far
        Seconds stayed { };           ///< At their goal so far
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

    /// One thing a stall sells.
    struct Ware
    {
        Product product = Product::Drink;
        Serve serve = Serve::Any;
        Money price { };
        std::string symbol { };  ///< Of the item a guest carries away, as `Burger:cid`; empty when not known
        std::uint8_t extras = 0; ///< How many extras (toppings, sauces, ice) its item can take
        std::uint8_t topped = 0; ///< How many of those go on it
        /// What those add to how much it feeds, quenches and sickens, each by how much of it goes on.
        std::array<float, 3> topping { };
        Money cost { }; ///< Of its stock, with the extras going on, to the park for each one sold
        /// Most a guest pays for it, with those extras, in mild weather, the cold and the heat; none when not known.
        std::array<Money, 3> worth { };
    };

    struct Shop
    {
        Product product = Product::Drink; ///< What it is visited for: its first ware's
        Serve serve = Serve::Any;
        /// From the middle of its entrance tile towards the stall, along x and z, one long; buyers stand at
        /// its counter that way. Zero when not known: they stand in the tile's middle.
        glm::vec2 counter { 0.0f };
        std::vector<Ware> menu { }; ///< Its food, drink and umbrellas; empty: one ware of `product` at its attraction's price
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

    /// A guest riding from one station of a ride to another on the way somewhere: what they were doing, and the
    /// rest of their way from the exit they get off at.
    struct Crossing
    {
        entt::entity ride = entt::null;
        Errand errand { };
        std::vector<NodeId> route { };
    };

    /// The ways riders walk on and off a ride, in the park's space, worked out from the ride's models where it
    /// is drawn. Seats are counted as a load fills them: a ride's riders, or a train's. A ride without one lets
    /// its riders on and off where they stand.
    struct RideWalks
    {
        glm::vec3 ticket { 0.0f };  ///< Where a guest stands at the entrance to hand over their ticket
        Radians ticket_heading { }; ///< The way they face there, as Pose::heading
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
        std::optional<save::RideGraphs> lap_done { };
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

    // ----- litter, path items and staff -----

    /// A piece of litter lying on a path tile until a handyman sweeps it up.
    struct Litter
    {
        NodeId node = kNoNode;
        /// What it looks like, numbered as saved parks number it (0..5).
        /// shortcut: which saved number is a cup or a wrapper is not known, so ours are 0 for food and 1 for
        /// drink; upgrade when litter is drawn.
        std::uint8_t kind = 0;
        Seconds age { };    ///< It has lain
        bool vomit = false; ///< Left by a sick guest
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

    enum class PathItemKind : std::uint8_t
    {
        Bin,
        Bench,
        Sign,
        Sight,        ///< Something to look at: an effect or an animatronic
        PhotoSpot,    ///< A view to stop at, where a player's guest has their photo taken
        Fountain,     ///< Jumping fountains, which guests passing turn to look at
        WaterCannon,  ///< A cannon a guest may stop to fire at those passing
        Speaker,      ///< A loudspeaker whose music guests passing hear
        PassportPost, ///< A stamp machine
    };

    /// Whether a vandal breaks a path item of `kind`: what guests sit on, bin into or read, not the features.
    [[nodiscard]] constexpr bool breakable(PathItemKind kind) noexcept
    {
        return kind == PathItemKind::Bin || kind == PathItemKind::Bench || kind == PathItemKind::Sign;
    }

    /// Something standing on a path tile that guests use and vandals break.
    struct PathItem
    {
        NodeId node = kNoNode;
        PathItemKind kind = PathItemKind::Bin;
        bool broken = false;      ///< Of no use until a handyman mends it, or the player replaces it
        std::uint64_t object = 0; ///< The placed object it stands as, 0 for none
        std::uint8_t seats = 2;   ///< On a bench, how many sit on it at once: its model's seats, two on the game's own
        float heading = 0;        ///< Where its model's +X points, round the vertical, 0 along +Z as Pose's: a water cannon's barrel at rest
    };

    /// In the registry's context: how much scenery stands round each path tile, by NodeId, as weighed by
    /// scenery_weight. Tiles laid since count as bare.
    /// shortcut: scenery taken away still counts till the park loads again; upgrade when removal reaches the
    /// simulation.
    struct SceneryNear
    {
        std::vector<float> by_node;
    };

    /// A flower bed beside a path. It dries out, and a handyman waters it once dry.
    struct FlowerBed
    {
        NodeId node = kNoNode;              ///< The path it is watered from
        std::uint64_t object = 0;           ///< The placed object it stands as
        float capacity = 0;                 ///< Water it holds when full, as its scenery data gives it
        float water = 0;                    ///< In the same measure as `capacity`
        entt::entity handyman = entt::null; ///< The one on the way to water it
    };

    /// A guest stopped on the path a while: sitting on a bench, or watching a sight.
    struct Linger
    {
        PathItemKind at = PathItemKind::Bench;
        Seconds left { };                 ///< Watching: till they walk on. Sitting: till they may get up, then getting up
        entt::entity bench = entt::null;  ///< The bench they sit on, the water cannon they fire, or the view they take in
        std::uint8_t seat = 0;            ///< Which of its seats, counting as its model numbers them
        Seconds sat { };                  ///< Since they sat down
        bool rising = false;              ///< Getting up, for what `left` holds
        entt::entity target = entt::null; ///< Firing a water cannon: who they spray, none while they aim about
        bool camera = false;              ///< At a view, taking the photo of the rest of their party
    };

    /// How a guest shows what they feel, standing a moment where they are.
    enum class GestureKind : std::uint8_t
    {
        Cheer,       ///< Jumping for joy off a ride they loved
        Laugh,       ///< Off a ride they loved
        Nod,         ///< Off a ride they liked well enough
        Grumble,     ///< Shaking their head at a ride too tame for them
        Dizzy,       ///< Reeling off a ride too wild for them
        Retch,       ///< Being sick
        Down,        ///< Knocked over, lying on the ground till they get up
        HandOver,    ///< Putting their rubbish in a bin, or an autograph between a VIP and a fan
        Clap,        ///< At the end of a good show
        LookWatch,   ///< Looking at their watch after a poor show
        Disapproval, ///< Arms folded at a poor show
        Cry,         ///< A child crying after a poor show
        Horror,      ///< A child taking fright at a poor show
    };

    struct Gesture
    {
        GestureKind kind = GestureKind::Nod;
        Seconds left { };
        entt::entity at = entt::null; ///< What it is made at, if anything: the bin their rubbish goes in
    };

    /// A look a guest gives something they walk on past, their head turned towards it a moment.
    struct Glance
    {
        entt::entity at = entt::null; ///< The path item looked at: jumping fountains
        Seconds left { };
    };

    /// A guest stopped to watch someone, the star: an entertainer's show, or a VIP they want an autograph from.
    /// They step across the path to a place on a ring round the star, at `angle` round it, and face them.
    struct Watching
    {
        entt::entity star = entt::null;
        Radians angle { };
        bool autograph = false; ///< Waiting for the star, a VIP, to sign for them
        bool chasing = false;   ///< A fan walking after the VIP over the paths, too far off to be signed yet
        Seconds waited { };
    };

    /// What an entertainer does in a show, in turn: the motions their costume has for it.
    enum class ShowMove : std::uint8_t
    {
        Wave,
        Idle,
        IdleTwo,
        DanceOne,
        DanceTwo,
        DanceThree,
        FallOver, ///< Going down, in a poor show
        GetUp,
        Dejected, ///< At the end of a poor show
    };

    /// An entertainer's shows: the one they put on now, if any, and the wait before the next.
    struct Show
    {
        std::vector<ShowMove> moves; ///< Empty between shows
        Seconds into { };            ///< Since the show began
        bool poor = false;           ///< Put on miserable or lazy: the crowd goes away unimpressed
        Seconds gap { };             ///< Before they look for a crowd again
    };

    /// The VIPs a guest has had an autograph from, so they do not ask again.
    struct Autographs
    {
        std::vector<entt::entity> from;
    };

    /// How a rider on a coaster shows what they feel.
    enum class RideReaction : std::uint8_t
    {
        Waiting,   ///< In the station or pulled up a lift
        Calm,      ///< Sitting through it
        HandsUp,   ///< Loving it, or floating over a hill
        CoverEyes, ///< Too wild for them, going down
        Strained,  ///< Pressed hard into their seat or sideways
    };

    /// A guest seated on a ride, and how they react to it now.
    struct Thrill
    {
        RideReaction reaction = RideReaction::Waiting;
    };

    /// A rider walking on to their seat from the ticket spot, or off it to the paths: still riding, but on
    /// their feet. `points` are walked in turn; the first, on the way on, is the ticket spot, where they stand
    /// for `pause` seconds first.
    struct RideWalk
    {
        std::vector<glm::vec3> points;
        std::size_t next = 0; ///< The point walked to now
        Seconds pause { };
        bool off = false;
        std::optional<GestureKind> gesture { }; ///< Shown once off, on the paths
    };

    /// A party member with no way over to the others: they stand looking about for `left` ticks, then are put
    /// with them.
    struct Lost
    {
        std::uint32_t left = 0;
    };

    /// A person the player has picked up, hanging under the cursor: out of the simulation's walking and
    /// queueing till let go, their party waiting for them. Where they hang is the player's to move (Pose).
    struct Held
    {
        Distance height { };   ///< Over the ground under them
        bool happy = false;    ///< Enjoying it, or cross
        bool swimwear = false; ///< Taken out of a pool in their swimwear, kept on till they land
    };

    /// A person let go of, thrown by a crash or sliding down ground grown too steep: falling to `ground`, then
    /// walking straight to `node` to go on from there. One thrown flies on across the ground as they fall, and
    /// lands knocked over; one sliding keeps to the ground, and falls over once they stop.
    struct Dropped
    {
        float ground = 0;     ///< World height they land at; under them as they fly, for one thrown
        Pace fall { };        ///< Downwards, now; below 0 while thrown upwards
        glm::vec2 drift { };  ///< World units a second across the ground, x and z
        bool thrown = false;  ///< Lands knocked over, and lies there a while before walking on
        bool sliding = false; ///< Down the slope, along the ground
        Seconds slid { };     ///< Since they began sliding
        NodeId node = kNoNode;
        bool swimwear = false;          ///< Taken out of a pool in their swimwear, kept on till they land
        entt::entity pool = entt::null; ///< The changing room of the pool complex they fall into, to swim there
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
        Other,    ///< A lifeguard: sits in their chair by a pool, off the paths
        Vendor,   ///< Sells at a stall or a ride's entrance, standing at their post off the paths
        Keeper,   ///< Looks after an animal enclosure, inside it off the paths
        /// Goes round the attractions in turn: rides them and stands at stalls, as a guest would but free, and
        /// tells the player what is wrong with them (see Rounds)
        Inspector,
        Entertainer, ///< Puts on shows for the guests near them (see Show)
    };

    /// A handyman's tasks among the task flags, numbered as saved parks number them.
    inline constexpr std::int32_t kSweepTask = 1 << 0;
    inline constexpr std::int32_t kWaterGardensTask = 1 << 1;
    inline constexpr std::int32_t kEmptyBinsTask = 1 << 2;
    /// Our own task, kept the other way round: set, a handyman leaves broken path items alone. Parks the game
    /// saved never set it, so their handymen mend.
    inline constexpr std::int32_t kLeaveBrokenTask = 1 << 3;
    /// A mechanic's tasks among the task flags, numbered as saved parks number them.
    inline constexpr std::int32_t kInspectRidesTask = 1 << 0;
    inline constexpr std::int32_t kFixRidesTask = 1 << 1;
    /// A keeper's tasks among the task flags, numbered as saved parks number them.
    inline constexpr std::int32_t kClearDungTask = 1 << 0;
    inline constexpr std::int32_t kFillTroughsTask = 1 << 1;
    /// What a handyman does when the park says nothing: every task of the game's.
    inline constexpr std::int32_t kEveryTask = 0b111;
    /// An inspector's tasks among the task flags, numbered as saved parks number them, taken in this order.
    inline constexpr std::int32_t kFlatRidesTask = 1 << 0;
    inline constexpr std::int32_t kTrackedRidesTask = 1 << 1;
    inline constexpr std::int32_t kRefreshmentsTask = 1 << 2; ///< Food and drink stalls, and how hungry, thirsty, tired and bursting the guests are
    inline constexpr std::int32_t kSouvenirsTask = 1 << 3;
    inline constexpr std::int32_t kVistasTask = 1 << 4; ///< Rides and stalls that do not fit the scenery round them
    inline constexpr std::int32_t kStaffTask = 1 << 5;  ///< Watching a worker at work
    inline constexpr std::int32_t kPoolsTask = 1 << 6;
    /// Viewing galleries, which parks here do not have yet: the task is passed over.
    inline constexpr std::int32_t kGalleriesTask = 1 << 7;

    /// What a worker's `busy` time goes on.
    enum class StaffJob : std::uint8_t
    {
        None,     ///< Nothing, or an entertainer standing for a show
        Sweep,    ///< Litter or sick on the path
        EmptyBin, ///< A bin on the path
        Mend,     ///< A broken path item
        Water,    ///< A flower bed
        Fix,      ///< A broken-down ride (Staff::ride)
        Inspect,  ///< A ride (Staff::ride)
        Watch,    ///< A guard stopped on their rounds, keeping an eye on the paths
        Rest,     ///< A lazy worker's rest
    };

    struct Staff
    {
        StaffRole role = StaffRole::Other;
        std::int32_t tasks = kEveryTask; ///< Task flags, as save::Staff keeps them
        Seconds busy { };                ///< Left at the job in hand
        entt::entity ride = entt::null;  ///< The ride a mechanic is going to, or working on, to repair or inspect
        Money wage { };                  ///< A month
        std::int32_t wage_level = 1;     ///< 0 to 2, what `wage` follows (staff_care.hpp); 1 as hired
        std::int32_t training = 3;       ///< 0 untrained to 3; a hire's is set by hired_training (staff_care.hpp)
        std::int32_t warnings = 0;
        Share happiness { 1 };
        Share laziness { };            ///< How much longer each job takes, and how often they stop to rest
        Share load { };                ///< Of the recent past, spent at work
        StaffJob job = StaffJob::None; ///< What `busy` is spent on
        Seconds job_length { };        ///< `busy` as the job began
        std::uint32_t mended = 0;      ///< Broken path items mended since the run began
        Seconds think { };             ///< To their next thought
        std::uint32_t miserable = 0;   ///< Thoughts in a row had while miserable
        // Work done since they were hired, as their window counts it.
        std::int32_t swept = 0;
        std::int32_t emptied = 0;
        std::int32_t watered = 0;
        std::int32_t fixed = 0;
        std::int32_t inspected = 0;
        std::int32_t cleared = 0; ///< Dung, by a keeper
        std::int32_t filled = 0;  ///< Troughs, by a keeper
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
    /// breakdown, which closes it until a mechanic repairs it. Inspections win some reliability back. Stalls and
    /// cash machines wear with time instead (`break_in`), and are never inspected.
    struct Upkeep
    {
        Share reliability { 1 };
        bool broken = false;
        Seconds since_inspection { };
        Seconds inspect_every { 600 };      ///< Negative never
        entt::entity mechanic = entt::null; ///< Sent to it, or at work on it
        bool called = false;                ///< The player asked for a mechanic, who inspects it as if due
        std::uint32_t breakdowns = 0;       ///< Since the run began
        Breakdown kind = Breakdown::None;   ///< The breakdown going on
        Breakdown last = Breakdown::None;   ///< The latest one, going on or fixed
        /// Downtime: each window is sampled a set number of times, and counts the samples the ride was down
        /// in. Newest first; the oldest drops off as a new window starts.
        std::array<std::uint8_t, 8> downtime { };
        std::uint8_t samples = 0; ///< Taken in the newest window
        Seconds sample_clock { }; ///< Since the last sample
        /// A stall's or cash machine's wear: what is left before it breaks down, counted down faster the fewer
        /// guards the park has for its guests. Zero for one to be rolled afresh; negative for rides and pools.
        Seconds break_in { -1 };

        /// Share of the samples in all windows taken while down, 0..1.
        [[nodiscard]] float downtime_share() const;
    };

    /// What the park has paid its staff, kept in the registry's context.
    struct Payroll
    {
        Money paid { };
        Seconds week { }; ///< Into the week, paid as it ends
    };

    /// What the park has paid for supplies, such as pools' cleaning fluid and stalls' stock, kept in the registry's
    /// context.
    struct Supplies
    {
        Money paid { };
    };

    /// What guests have paid the park for rides, shops and entry, kept in the registry's context.
    struct Receipts
    {
        Money paid { };
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

    /// What an inspector found wrong.
    enum class Finding : std::uint8_t
    {
        TooDear,    ///< Something at a stall costs more than guests pay for it
        TooCheap,   ///< Something at a stall sells for far less than it is worth
        LongQueue,  ///< They queued for ages at a ride
        NoPath,     ///< The attraction's entrance is on no path
        NoMechanic, ///< A broken ride with no mechanic near
        OffTheme,   ///< A ride or stall unlike the scenery round it
        Lazy,       ///< A worker they watched (the finding's entity)
        Unhappy,    ///< Likewise
        DirtyPool,  ///< At the changing room of a pool they swam in
        FilthyPool,
        Hungry, ///< Many of the guests; with no entity, as are the next three
        Thirsty,
        Tired,
        Bursting,           ///< Needing the toilet
        NoMechanicCanReach, ///< A broken attraction no free mechanic has a way to
        BadZones,           ///< A broken attraction outside every mechanic's patrol, all of them having one
        Breakdowns,         ///< Many of the attractions broken down at once; with no entity
        NoPathToStaff,      ///< A worker they found no way to
        NoKeeperCanReach,   ///< An animal house wanting its troughs filled that no keeper of its enclosure is set to fill
        KeeperBadZones,     ///< One that every such keeper has a patrol leaving out
        TroughsEmpty,       ///< A hungry animal (the finding's entity) finding its house's food trough empty, time and again
        TooMuchDung,        ///< An animal house whose enclosure has more dung than its animals put up with
        // Not the inspector's: a worker's own news (staff_care.hpp), with the worker
        WantsToQuit,
        Quit, ///< The park lets them go once told
    };

    /// Where an inspector is in their rounds. On a visit they carry a guest's Needs, Mood and Wallet, and the
    /// guests' code walks, queues and rides them; let go, they are staff again.
    struct Rounds
    {
        std::uint8_t task = 0; ///< The next of their tasks to look at, by its bit among the task flags
        Seconds gap { };       ///< Before they next look for somewhere to go
        /// The attractions visited and workers watched, with when (seconds into the run).
        std::vector<std::pair<entt::entity, float>> seen { };
        entt::entity watching = entt::null; ///< The worker they are after, or watching
        float chased_from = 0;              ///< When they set off after them
        float watched_from = -1;            ///< When they came close enough to watch; -1 not yet
        /// What they told the player about what, with when: not told again for a while.
        std::vector<std::tuple<entt::entity, Finding, float>> told { };
    };

    /// The inspectors' findings and the workers' own news, oldest first, in the registry's context, for the park
    /// to tell the player.
    struct Findings
    {
        std::vector<std::pair<Finding, entt::entity>> found { }; ///< With the attraction or worker
    };

    struct Vandalism
    {
        std::uint32_t broken = 0;     ///< Things broken since the run began
        float recent = 0;             ///< Things broken lately, each fading away over a game hour or so
        Seconds since_warning { -1 }; ///< Since the player was last told; -1 never
        std::uint32_t warnings = 0;   ///< Times the player has been told, for the park to post as messages
    };

} // namespace rct3::simulation
