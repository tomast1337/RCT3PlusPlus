// SPDX-License-Identifier: GPL-3.0-or-later
//
// The numbers the simulation runs on, grouped by what they shape. Values are our own, first guesses tuned by
// watching parks run, unless a comment names where they come from.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace rct3::simulation::tuning
{

    /// Needs and moods while walking about.
    struct Needs
    {
        float thirst_per_second = 1.0f / 480; ///< From satisfied to desperate in eight minutes
        float hunger_per_second = 1.0f / 720;
        float bladder_per_second = 1.0f / 900;
        float energy_per_second = 1.0f / 3600; ///< Lost while walking about
        float gloom_per_second = 1.0f / 600;   ///< Happiness lost while a need is pressing
        float pressing = 0.7f;                 ///< A need this high spoils the guest's mood
        float want = 0.55f;                    ///< A need this high sends the guest to a shop
        double ride_chance = 0.25;             ///< Of a guest with no pressing need going for a ride
        float max_nausea = 0.6f;               ///< Guests feeling sicker skip rides
        float idle_seconds = 5;                ///< Between thoughts while wandering
        float treat = 0.05f;                   ///< Happiness from buying what was wanted
    };

    /// Walking the paths and in parties.
    struct Walking
    {
        float lane_drift = 0.4f;     ///< Most a walker's lane moves across the path from one tile to the next
        float catch_up = 1.5f;       ///< How much faster than their walk a follower makes up ground
        double join_chance = 0.6;    ///< Of a party member going on the ride their leader picked
        double huddle_chance = 0.08; ///< Of a wandering party stopping to stand together, each thought
        float shortest_huddle = 8;   ///< Seconds
        float longest_huddle = 20;
        float huddle_radius = 0.7f; ///< Of the ring a party stands in, world units
    };

    /// Queues. Tuned by eye against busy saved parks, whose queue tiles hold 7 to 16 waiting guests two to four abreast.
    struct Queues
    {
        float spacing = 0.08f;                ///< Tiles along the line from a guest to the one ahead
        float reach = 0.9f;                   ///< Farthest a queueing guest stands off the line's middle, world units
        float gap_spread = 0.4f;              ///< Of a gap, how far guests stand off their even places in line, all told
        float shuffle = 0.12f;                ///< Tiles the gap ahead opens before a guest steps up
        float give_up_after = 480;            ///< Seconds of queueing after which an unhappy guest may give up
        float give_up_mood = 0.3f;            ///< Happiness below which they may
        float give_up_per_second = 1.0f / 30; ///< Chance a second of giving up then
    };

    /// Cash machines, benches and sights.
    struct Facilities
    {
        std::int64_t low_cash = 500;  ///< Hundredths; a guest with less goes to a cash machine
        std::int64_t withdraw = 5000; ///< Hundredths a guest draws from one
        float tired = 0.45f;          ///< Energy below which a guest sits down on a bench they pass
        double sit_chance = 0.5;
        float sit_seconds = 30;
        float rest_per_second = 1.0f / 60; ///< Energy back while sitting
        std::size_t bench_seats = 3;
        double watch_chance = 0.3; ///< Of stopping at a sight passed
        float watch_seconds = 15;
        float sight_cheer_per_second = 1.0f / 300; ///< Happiness while watching
    };

    /// Nausea and being sick.
    struct Sickness
    {
        float settle_per_second = 1.0f / 900;      ///< Nausea fading away
        float rest_settle_per_second = 1.0f / 120; ///< And while sitting on a bench
        float wildest = 0.3f;                      ///< Intensity past a guest's tolerance they still go on
        float full_stomach = 1.5f;                 ///< Nausea a ride gives a guest still eating, times as much; settling as much slower
        float sick = 0.8f;                         ///< Nausea past which a guest may be sick
        double sick_after_ride = 0.3;              ///< Chance of it, getting off a ride
        double sick_per_second = 1.0 / 120;        ///< Chance a second, walking
        float relief = 0.25f;                      ///< Nausea gone with it
        float vomit_gloom_per_second = 1.0f / 300; ///< Happiness lost beside vomit
        float tolerance_per_saved_level = 0.25f;   ///< Saves keep one of four stomach tolerances; each is this much more
        float queasy_per_second = 1.0f / 600;      ///< Nausea gained beside it
    };

    /// Rain, heat and cold on guests.
    struct Weather
    {
        float wet_per_second = 1.0f / 120;         ///< Out in the rain with nothing over them; twice that in a storm
        float dry_per_second = 1.0f / 300;         ///< Out of it
        float wet_gloom_per_second = 1.0f / 400;   ///< Happiness lost while wet; twice that in the cold
        float covered = 0.5f;                      ///< Share of a ride under cover that keeps its riders dry
        double rain_leave_per_second = 1.0 / 90;   ///< Chance a second of leaving an open-air ride's queue; three times that in a storm
        float hot = 25;                            ///< Degrees from which guests get thirsty faster
        float cold = 10;                           ///< Degrees under which guests get hungry faster
        float heat_per_degree = 0.1f;              ///< Extra thirst, or hunger in the cold, per degree past those
        float sweat_per_second = 1.0f / 900;       ///< Heat gained out in the sun on a hot day, more for each degree past weather.hot
        float cool_per_second = 1.0f / 90;         ///< Heat lost in the shade, on a cooler day, or while wet
        float heat_gloom_per_second = 1.0f / 1200; ///< Happiness lost while too hot
        float splash = 0.3f;                       ///< Wetness a splashing ride leaves its riders with: damp, not soaked
    };

    /// Day and night.
    struct Night
    {
        float dusk = 20.0f / 24;
        float dawn = 6.0f / 24;
        float thrill = 0.3f; ///< Extra excitement teens find in a coaster in the dark
        float dread = 0.2f;  ///< Excitement families lose in one
        float daring = 0.2f; ///< Intensity past their tolerance teens take on at night
    };

    /// Guests coming in at the gate, and what draws them.
    struct Arrivals
    {
        double per_second = 1.0 / 30; ///< Guests coming in at the gate
        double family_share = 0.15;   ///< Of those at night that are not teens; teens the same by day
        double rain = 0.5;
        double storm = 0.25;
        double cold = 0.6;
        double sunny = 1.3; ///< On a sunny day that is not too cold
        // Pools: a series of parks saved a step apart (an empty pool, then filled, deeper, wider, with a jacuzzi,
        // extras, a slide, a lazy river, a second pool) shows the ideal guest count growing by three for each square
        // holding water, whatever its depth, and by nothing for jacuzzis or extras.
        std::uint32_t guests_per_pool_square = 3;
        std::uint32_t guests_per_seat = 3; ///< For a ride whose data does not say how many it makes room for
        // Rides and pools alone reproduce the saved parks' ideal guest counts, exactly in small parks, a little
        // under in big ones, where some rides are not simulated yet; we give some room over on purpose: better too many
        // guests than too few.
        double room_to_spare = 1.1;
        float fill_seconds = 400;      ///< A park drawing guests at the plain rate fills its room in this long: ten game hours
        float plain_rating = 0.6f;     ///< Rating at which guests come at the plain rate; also when unrated
        float unknown_rating = 0.25f;  ///< Rating under which a park draws only the few curious
        float curious = 0.1f;          ///< Those few, as rating
        float famous_rating = 0.9f;    ///< Rating past which a better park draws no more
        double crowded = 0.3;          ///< Of the guests still coming once the park is full
        double overpriced = 0.3;       ///< Of them, once the fee is over fair; again past twice fair
        std::int64_t ride_worth = 400; ///< Hundredths, to a guest, of a ride at full excitement
        float intensity_worth = 0.3f;  ///< Of that for each of intensity
        float too_intense = 0.9f;      ///< Intensity past which a ride is worth half to most
        double fair_fee_share = 1.5;   ///< Of what the rides are worth over their prices
        float mood = 0.7f;
        float teen_tolerance = 0.75f;
        std::int64_t cash = 5000; ///< Hundredths
    };

    /// Guests going home. Tuned so busy saved parks keep their guest counts.
    struct Leaving
    {
        double night_per_second = 1.0 / 60;      ///< Chance a second of a family heading home in the dark
        double storm_per_second = 1.0 / 120;     ///< Chance a second of any guest heading home in a storm
        float fed_up = 0.25f;                    ///< Happiness under which a guest has had enough
        double fed_up_per_second = 1.0 / 60;     ///< Chance a second of such a guest heading home
        float worn_out = 0.2f;                   ///< Energy under which a guest not enjoying themselves tires of it
        float enjoying = 0.5f;                   ///< Happiness from which a worn out guest stays on
        double worn_out_per_second = 1.0 / 90;   ///< Chance a second of a worn out guest heading home
        float long_stay_hours = 4;               ///< Game hours in the park after which guests start to drift home
        double long_stay_per_second = 1.0 / 400; ///< Chance a second of it after that, twice that after twice as long
        double broke_per_second = 1.0;           ///< A guest who cannot pay for anything and has no cash machine goes at once
    };

    /// Thoughts.
    struct Thoughts
    {
        std::size_t kept = 6;        ///< As many as saved parks keep
        float rethink_seconds = 300; ///< Before the same thought about the same thing comes back
        float enjoyed = 0.5f;        ///< Excitement of a ride its riders enjoy
    };

    /// Litter and bins.
    struct Litter
    {
        std::size_t dirty_path = 3; ///< Pieces of litter on a tile that make guests on it glum
        float dirt_gloom_per_second = 1.0f / 900;
        float carry_seconds = 60;       ///< A guest looks for a bin this long before dropping their rubbish
        double litterbug_chance = 0.03; ///< Of dropping it at once, for a happy guest
        float old_seconds = 400;        ///< Litter lying this long counts against the park's rating
        double grumpy_chance = 0.3;     ///< Added as happiness falls to nothing
    };

    /// Vandals and guards.
    struct Vandals
    {
        float gloom_per_second = 1.0f / 1200; ///< While standing by something broken
        float mood = 0.15f;                   ///< Guests less happy than this may break things
        double per_second = 1.0 / 120;        ///< Chance a second of such a guest breaking what is by them
        int guard_reach = 3;                  ///< Path tiles a guard keeps vandals off, walking out from theirs
        float fade = 3600;                    ///< Seconds over which a break stops counting as recent
        float bad = 2.5f;                     ///< Recent breaks that make the player be told: about three within the hour
        float warning_every = 1200;           ///< Seconds at least between telling them
    };

    /// Handymen at work.
    struct Handymen
    {
        float sweep_seconds = 2; ///< A piece of litter
        float empty_seconds = 4; ///< A bin
        float mend_seconds = 8;
        float empty_at = 0.5f; ///< Share of a bin's room filled before a handyman empties it
    };

    /// Ride wear, mechanics and flow. To tune against saved rides' reliability and downtime.
    struct Wear
    {
        float per_run = 0.004f;          ///< Reliability a run costs
        double breakdown_per_run = 0.05; ///< Chance a run ends in a breakdown, times the reliability lost
        float breakdown_gloom = 0.1f;    ///< Happiness lost by those queueing when their ride breaks down
        float repair_seconds = 20;
        float inspect_seconds = 10;
        float repair_gain = 0.02f;    ///< Reliability a repair wins back
        float inspect_gain = 0.05f;   ///< And an inspection
        float flow_smoothing = 0.25f; ///< Weight of the newest departure in a ride's riders an hour
    };

    /// How content and lazy staff are, and their pay and training.
    struct Staff
    {
        std::int64_t lowest_wage = 50;      ///< Hundredths an hour at level 0; each level adds half as much
        float load_seconds = 600;           ///< Over how long their share of time at work is judged
        float content_base = 0.45f;         ///< Happiness of an untrained worker on the lowest wage
        float content_per_training = 0.1f;  ///< More for each level of training
        float content_per_wage = 0.1f;      ///< More for each wage level
        float overworked = 0.85f;           ///< Share of time at work past which they feel overworked
        float bored = 0.15f;                ///< Share below which they feel idle
        float overwork_gloom = 2;           ///< Happiness lost for each share past overworked
        float bored_gloom = 3;              ///< And for each share below idle
        float mood_per_second = 1.0f / 900; ///< How fast happiness goes to where it is heading
        float lazy_below = 0.5f;            ///< Happiness below which laziness grows
        float lazy_per_second = 1.0f / 600; ///< Laziness grown a second for each share below that, untrained
        float lazy_fade_per_second = 1.0f / 3600;
        float lazy_after_warning = 0.3f;                                  ///< Laziness at which a warning is deserved
        float warning_gloom = 0.2f;                                       ///< Happiness a warning not deserved takes away
        float rest_per_second = 1.0f / 60;                                ///< Chance to rest a second, for a worker wholly lazy
        std::array<std::int64_t, 3> training_cost { 5000, 10000, 20000 }; ///< Hundredths, for each level from untrained up
    };

    /// The park rating: each part's share of the 1000, and the shapes inside them.
    struct Rating
    {
        float happiness_share = 500;
        float rides_share = 300;
        float reliability_share = 100;
        float guests_share = 300;
        float empty_park_happiness = 0.5f;
        float good_excitement = 7;   ///< A ride this exciting scores in full
        float harsh_intensity = 7;   ///< Above this, a ride starts to put guests off
        float intensity_range = 4;   ///< How far above harsh a ride stops scoring at all
        float rides_for_variety = 3; ///< About two thirds of the variety at this many rides
        float guests_for_busy = 300; ///< About two thirds of the guests part at this many guests
        std::int32_t per_litter = 3;
        std::int32_t most_litter = 500;
        float harder_share = 0.15f; ///< Taken off the score by the harder rating
        float weekly_move = 0.5f;   ///< Share of the way the rating moves towards the week's score
    };

    struct Tuning
    {
        Needs needs;
        Walking walking;
        Queues queues;
        Facilities facilities;
        Sickness sickness;
        Weather weather;
        Night night;
        Arrivals arrivals;
        Leaving leaving;
        Thoughts thoughts;
        Litter litter;
        Vandals vandals;
        Handymen handymen;
        Wear wear;
        Staff staff;
        Rating rating;
    };

} // namespace rct3::simulation::tuning

namespace rct3::simulation
{
    inline constexpr tuning::Tuning kTuning { };
} // namespace rct3::simulation
