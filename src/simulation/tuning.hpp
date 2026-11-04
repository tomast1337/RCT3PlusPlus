// SPDX-License-Identifier: GPL-3.0-or-later
//
// The numbers the simulation runs on, grouped by what they shape. Values are our own, first guesses tuned by
// watching parks run, unless a comment names where they come from. Each value's type says what it measures
// (see units.hpp).
#pragma once

#include "simulation/units.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace rct3::simulation::tuning
{

    /// Needs and moods while walking about.
    struct Needs
    {
        PerSecond thirst = PerSecond::over({ 480 }); ///< From satisfied to desperate in eight minutes
        PerSecond hunger = PerSecond::over({ 720 });
        PerSecond bladder = PerSecond::over({ 900 });
        PerSecond tiring = PerSecond::over({ 3600 }); ///< Energy lost while walking about
        PerSecond gloom = PerSecond::over({ 600 });   ///< Happiness lost while a need is pressing
        Share pressing { 0.7f };                      ///< A need this high spoils the guest's mood
        Share want { 0.55f };                         ///< A need this high sends the guest to a shop
        Chance ride_chance { 0.25 };                  ///< Of a guest with no pressing need going for a ride
        Chance ride_considered { 0.5 };               ///< Of each ride that suits being thought of when choosing one
        Share max_nausea { 0.6f };                    ///< Guests feeling sicker skip rides
        Seconds idle { 5 };                           ///< Between thoughts while wandering
        Share treat { 0.05f };                        ///< Happiness from buying what was wanted
    };

    /// Guests showing how a ride left them as they step off.
    struct Gestures
    {
        Chance chance { 0.6 };  ///< Of a guest who thinks something of a ride showing it
        Share liked { 0.25f };  ///< How much riders felt a ride they nod at, short of enjoying it
        Seconds shown { 1.8f }; ///< They stand for
    };

    /// Walking the paths and in parties.
    struct Walking
    {
        /// World units a second a child, a teen and an adult walk at. Their legs keep pace whatever the speed.
        std::array<Pace, 3> speed { Pace { 0.85f }, Pace { 1.25f }, Pace { 1.2f } };
        Distance lane_drift { 0.4f };  ///< Most a walker's lane moves across the path from one tile to the next
        Factor catch_up { 1.5 };       ///< How much faster than their walk a follower makes up ground
        Chance join_chance { 0.6 };    ///< Of a party member going on the ride their leader picked
        Chance huddle_chance { 0.08 }; ///< Of a wandering party stopping to stand together, each thought
        Seconds shortest_huddle { 8 };
        Seconds longest_huddle { 20 };
        Distance huddle_radius { 0.7f };  ///< Of the ring a party stands in
        Distance ring_gap { 0.9f };       ///< Between neighbours standing in a ring, so a big party's ring is wider
        Seconds regroup { 3 };            ///< A party stands together once all are back, before going on
        Distance corner_cut { 0.8f };     ///< Chosen by eye: how far towards the inside of a turn a walker going somewhere takes the tile they turn on
        float turn_rate = 6.0f;           ///< Chosen by eye: fastest a walker turns to face where they go, radians a second
        float swerve = 0.5f;              ///< Chosen by eye: most a walker steps aside from others for each unit walked on
        Seconds retry_way { 5 };          ///< Chosen by eye: how long one who found no way back to their party waits before looking again
        Seconds lost_wait { 20 };         ///< Chosen by eye: how long one cut off from their party stands lost before being put with them
        std::size_t gather_reach = 24;    ///< Tiles looked at for one a party can wait on of its own
        Distance personal_space { 0.6f }; ///< People walking nearer one another than this step apart
        Pace step_aside { 1.5f };         ///< Fastest anyone steps apart from others
        Factor keep_right { 1.0 };        ///< How much of stepping apart goes to the right, so two meeting head on pass
    };

    /// How fast guests go in each gait, and when they take it.
    struct Gaits
    {
        Factor drained { 0.8 }; ///< Of a guest's walk, with no energy left
        Factor fresh { 1.15 };  ///< And with all their energy
        /// Of a child, a teen and an adult with all their energy running the next stretch, each tile; fewer
        /// as their energy goes. Fitted to saved parks, where about a quarter of children run, one teen in
        /// twelve and few adults, at the half energy saved guests mostly have.
        std::array<Chance, 3> run { Chance { 0.45 }, Chance { 0.16 }, Chance { 0.03 } };
        Share weary { 0.2f };     ///< Energy below which a guest drags their feet
        Share bursting { 0.85f }; ///< Bladder over which a guest hurries, legs together
        Factor run_pace { 2.2 };  ///< Of their walk
        Factor weary_pace { 0.7 };
        Factor queasy_pace { 0.75 };
        Factor bursting_pace { 1.1 };
    };

    /// Riders reacting to the ride, on coasters and flat rides.
    struct Reactions
    {
        Share thrilled { 0.7f };                      ///< Chosen by eye: happiness over which a rider throws their hands up
        Frequency hands_up = Frequency::every({ 1 }); ///< A thrilled rider's chance of raising them, a second
        Frequency floating = Frequency::every({ 2 }); ///< Any rider's, floating over a hill
        float airtime = 0.3f;                         ///< Up through their seat, in g, under which riders float
        float strain = 3.5f;                          ///< g up through their seat a rider braces against
        float strain_across = 1.2f;                   ///< And sideways
        float hands_down = 4.5f;                      ///< g, up or sideways, that brings raised hands down
        /// Chosen by eye, for about a coaster rider in four under way with their hands up: of riders, the share
        /// never raising their hands, in the front car and behind it.
        Share timid_front { 0.15f };
        Share timid { 0.3f };
        Share stoic { 0.3f };     ///< Of riders, the share who never brace or tense up
        Share too_wild { 0.15f }; ///< Ride intensity past what a rider likes that has them cover their eyes
        Share hide_eyes { 0.5f }; ///< Of riders it is too wild for, the share who cover them
        Share suits { 0.5f };     ///< Chosen by eye: intensity short of what a flat ride's rider likes most that still thrills them
        float diving = -0.5f;     ///< Of the track's way, the drop under which a car heads down
        float still = 0.5f;       ///< m/s under which a train counts as standing, held at a brake or block
    };

    /// Queues. Tuned by eye against busy saved parks, whose queue tiles hold 7 to 16 waiting guests two to four abreast.
    struct Queues
    {
        Tiles spacing { 0.08f };                      ///< Along the line from a guest to the one ahead
        Distance reach { 0.9f };                      ///< Farthest a queueing guest stands off the line's middle
        Share way_out { 0.5f };                       ///< Of `reach`, how far riders walking off a ride keep off its way's middle: its steps and gates are narrower than a path
        Share gap_spread { 0.4f };                    ///< Of a gap, how far guests stand off their even places in line, all told
        Tiles shuffle { 0.12f };                      ///< The gap ahead opens this much before a guest steps up
        Seconds give_up_after { 480 };                ///< Of queueing, after which an unhappy guest may give up
        Share give_up_mood { 0.3f };                  ///< Happiness below which they may
        Frequency give_up = Frequency::every({ 30 }); ///< How often they do then
        Seconds ticket { 2.5f };                      ///< A guest stands handing over their ticket at a ride's entrance
    };

    /// Cash machines, benches and sights.
    struct Facilities
    {
        Money low_cash { 500 };     ///< A guest with less goes to a cash machine
        Money withdraw { 5000 };    ///< A guest draws from one, up to what their bank has left
        Money toilet_worth { 300 }; ///< Chosen by eye: most a guest bursting to go pays for a toilet; less as the need is less
        Share tired { 0.45f };      ///< Energy below which a guest sits down on a bench they pass
        Chance sit_chance { 0.5 };
        Seconds sit { 30 };                       ///< Least a guest sits before they think of getting up
        PerSecond rest = PerSecond::over({ 60 }); ///< Energy back while sitting
        /// Chosen by eye: once rested, a guest of full energy gets up in about this long; the more worn out, the longer.
        PerSecond stand = PerSecond::over({ 20 });
        float chatting = 3;          ///< Chosen by eye: how many times longer one sits on with someone beside them
        Seconds rise { 2.5f };       ///< Chosen by eye: getting up off a bench and stepping back to the path
        Chance watch_chance { 0.3 }; ///< Of stopping at a sight passed
        Seconds watch { 15 };
        Seconds served { 4 };                             ///< A party member at a stall's counter while the others wait
        Seconds relief { 12 };                            ///< Chosen by eye: at a toilet or first aid for a guest needing it fully
        PerSecond sight_cheer = PerSecond::over({ 300 }); ///< Happiness while watching
    };

    /// Guests eating and drinking what they bought.
    struct Eating
    {
        std::uint8_t bites = 6; ///< Chosen by eye: in which a guest eats or drinks something up
        Seconds bite { 2.5f };  ///< Chosen by eye: between bites
        // Chosen by eye: over the stalls' usual prices in the saves, so only prices raised past them put guests off.
        Money food_worth { 500 };   ///< Most a guest pays for something to eat
        Money drink_worth { 300 };  ///< Most a guest pays for a drink
        Share good_value { 0.35f }; ///< Chosen by eye: of its worth, at or under which a guest thinks a price good value
        /// Chosen by eye: by an extra's level (none, a little, some, more, loads, as the guest likes), how much of
        /// its usual amount goes on.
        std::array<float, 6> extra_amounts { 0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 1.0f };
        std::uint8_t toppings_noticed = 2; ///< Chosen by eye: extras an item must take for a guest to remark on them
        Chance topping_thought { 0.15 };   ///< Chosen by eye: of remarking on its extras, none or all, on buying it
    };

    /// Nausea and being sick.
    struct Sickness
    {
        PerSecond settle = PerSecond::over({ 900 });      ///< Nausea fading away
        PerSecond rest_settle = PerSecond::over({ 120 }); ///< And while sitting on a bench
        Share wildest { 0.3f };                           ///< Intensity past a guest's tolerance they still go on
        Factor full_stomach { 1.5 };                      ///< Nausea a ride gives a guest still eating; settling as much slower
        Share sick { 0.8f };                              ///< Nausea past which a guest may be sick
        Chance sick_after_ride { 0.3 };                   ///< Of it, getting off a ride
        Frequency sick_walking = Frequency::every({ 120 });
        Share relief { 0.25f };                           ///< Nausea gone with it
        PerSecond vomit_gloom = PerSecond::over({ 300 }); ///< Happiness lost beside vomit
        Share tolerance_per_saved_level { 0.25f };        ///< Saves keep one of four stomach tolerances; each is this much more
        PerSecond queasy = PerSecond::over({ 600 });      ///< Nausea gained beside it
    };

    /// Rain, heat and cold on guests.
    struct Weather
    {
        PerSecond wetting = PerSecond::over({ 120 });     ///< Out in the rain with nothing over them; twice that in a storm
        PerSecond drying = PerSecond::over({ 300 });      ///< Out of it
        PerSecond wet_gloom = PerSecond::over({ 400 });   ///< Happiness lost while wet; twice that in the cold
        Share covered { 0.5f };                           ///< Of a ride under cover, enough to keep its riders dry
        Frequency rain_leave = Frequency::every({ 90 });  ///< Guests leaving an open-air ride's queue; three times that in a storm
        Degrees hot { 25 };                               ///< From which guests get thirsty faster
        Degrees cold { 10 };                              ///< Under which guests get hungry faster
        Share heat_per_degree { 0.1f };                   ///< Extra thirst, or hunger in the cold, per degree past those
        PerSecond sweat = PerSecond::over({ 900 });       ///< Heat gained out in the sun on a hot day, more for each degree past `hot`
        PerSecond cooling = PerSecond::over({ 90 });      ///< Heat lost in the shade, on a cooler day, or while wet
        PerSecond heat_gloom = PerSecond::over({ 1200 }); ///< Happiness lost while too hot
        Share splash { 1.0f };                            ///< Wetness a splashing ride leaves its riders with: soaked through
    };

    /// Day and night.
    struct Night
    {
        Share dusk { 20.0f / 24 }; ///< Of the day gone
        Share dawn { 6.0f / 24 };
        Share thrill { 0.3f }; ///< Extra excitement teens find in a coaster in the dark
        Share dread { 0.2f };  ///< Excitement families lose in one
        Share daring { 0.2f }; ///< Intensity past their tolerance teens take on at night
    };

    /// Guests coming in at the gate, and what draws them.
    struct Arrivals
    {
        Frequency rate = Frequency::every({ 30 }); ///< Guests coming in at the gate, at the plain rate
        Factor rain { 0.5 };
        Factor storm { 0.25 };
        Factor cold { 0.6 };
        Factor sunny { 1.3 }; ///< On a sunny day that is not too cold
        // Pools: a series of parks saved a step apart (an empty pool, then filled, deeper, wider, with a jacuzzi,
        // extras, a slide, a lazy river, a second pool) shows the ideal guest count growing by three for each square
        // holding water, whatever its depth, and by nothing for jacuzzis or extras.
        std::uint32_t guests_per_pool_square = 3;
        std::uint32_t guests_per_seat = 3; ///< For a ride whose data does not say how many it makes room for
        // Rides and pools alone reproduce the saved parks' ideal guest counts, exactly in small parks, a little
        // under in big ones, where some rides are not simulated yet; we give some room over on purpose: better too many
        // guests than too few.
        Factor room_to_spare { 1.1 };
        Seconds fill { 400 };            ///< A park drawing guests at the plain rate fills its room in this long: ten game hours
        Share plain_rating { 0.6f };     ///< Rating at which guests come at the plain rate; also when unrated
        Share unknown_rating { 0.25f };  ///< Rating at or under which a park is unknown
        Share unknown_draw { 2.0f / 9 }; ///< Of the plain rate, what an unknown park still draws: the curious
        Share famous_rating { 0.9f };    ///< Rating past which a better park draws no more
        Factor crowded { 0.3 };          ///< On the guests still coming once the park is full
        Factor overpriced { 0.3 };       ///< On them once the fee is over fair; again past twice fair
        Money ride_worth { 400 };        ///< To a guest, of a ride at full excitement
        Share intensity_worth { 0.3f };  ///< Of that for each of intensity
        Share too_intense { 0.9f };      ///< Intensity past which a ride is worth half to most
        Factor fair_fee { 1.5 };         ///< On what the rides are worth over their prices
        Share mood { 0.7f };             ///< Happiness guests come in with
        Share teen_tolerance { 0.75f };
        Money cash { 5000 };        ///< What a guest brings in, where the park sets no average
        Money bank { 10000 };       ///< What cash machines can give a guest, where the park sets no average
        Share cash_spread { 0.2f }; ///< How far either side of the average a guest's cash and bank lie
    };

    /// The intensity of rides a kind of party likes, 0..1 as rides are rated: the middle of each guest's
    /// least and most; each guest's lie up to Parties::taste_spread either side.
    struct TasteBand
    {
        Share least { 0 };
        Share most { 1 };
    };

    /// The parties that come in at the gate.
    struct Parties
    {
        /// How often each PartyKind comes against the others, where the park sets no mix of its own.
        std::array<float, 8> weights { 2, 2, 3, 3, 2, 2, 4, 1 };
        Chance single_parent { 0.2 };   ///< Of a family having one parent
        std::int64_t most_children = 4; ///< In a family or on a day trip; at least one
        std::int64_t most_friends = 5;  ///< In a gang or a party; at least two
        Factor night_others { 0.15 };   ///< On parties other than teens' after dark
        /// How far the park's rides tilt the mix: a park of only the wildest rides draws teens' parties
        /// (1 + pull) times as often and families and day trips that much less; of only the gentlest, the reverse.
        double pull = 1.0;
        TasteBand thrill { { 0.2f }, { 0.85f } }; ///< Teens on their own and in gangs
        TasteBand middle { { 0.1f }, { 0.7f } };  ///< Couples, adults on their own and in parties
        TasteBand gentle { { 0.0f }, { 0.45f } }; ///< Families and day trips
        Share taste_spread { 0.1f };
    };

    /// Guests going home. Tuned so busy saved parks keep their guest counts.
    struct Leaving
    {
        // How often a guest heads home, for each reason.
        Frequency night = Frequency::every({ 60 });  ///< A family, in the dark
        Frequency storm = Frequency::every({ 120 }); ///< Any guest, in a storm
        Share fed_up { 0.25f };                      ///< Happiness under which a guest has had enough
        Frequency fed_up_leave = Frequency::every({ 60 });
        Share worn_out { 0.2f }; ///< Energy under which a guest not enjoying themselves tires of it
        Share enjoying { 0.5f }; ///< Happiness from which a worn out guest stays on
        Frequency worn_out_leave = Frequency::every({ 90 });
        float long_stay_hours = 4;                       ///< Game hours in the park after which guests start to drift home
        Frequency long_stay = Frequency::every({ 400 }); ///< After that; twice as often after twice as long
        Frequency broke = Frequency::every({ 1 });       ///< A guest who cannot pay for anything and has no cash machine: at once
    };

    /// Thoughts.
    struct Thoughts
    {
        std::size_t kept = 6;    ///< As many as saved parks keep
        Seconds rethink { 300 }; ///< Before the same thought about the same thing comes back
        Share enjoyed { 0.5f };  ///< How much riders must feel a ride to enjoy it
    };

    /// Litter and bins.
    struct Litter
    {
        std::size_t dirty_path = 3;                      ///< Pieces of litter on a tile that make guests on it glum
        PerSecond dirt_gloom = PerSecond::over({ 900 }); ///< Happiness lost on a dirty path
        /// An empty is held this long before it goes in a pocket, or on the path when they are full. Fitted to the
        /// saves: after five minutes, parks have about as much litter lying about as their saves had.
        Seconds carry { 60 };
        Seconds old { 400 }; ///< Litter lying this long counts against the park's rating
    };

    /// Vandals and guards.
    struct Vandals
    {
        PerSecond gloom = PerSecond::over({ 1200 });    ///< Happiness lost standing by something broken
        Share mood { 0.15f };                           ///< Guests less happy than this may break things
        Frequency breaking = Frequency::every({ 120 }); ///< Such a guest breaking what is by them
        int guard_reach = 3;                            ///< Path tiles a guard keeps vandals off, walking out from theirs
        Seconds fade { 3600 };                          ///< Over which a break stops counting as recent
        float bad = 2.5f;                               ///< Recent breaks that make the player be told: about three within the hour
        Seconds warning_every { 1200 };                 ///< At least, between telling them
    };

    /// Handymen at work.
    struct Handymen
    {
        Seconds sweep { 2 }; ///< A piece of litter
        Seconds empty { 4 }; ///< A bin
        Seconds mend { 8 };
        Seconds water { 6 };     ///< A flower bed
        Share empty_at { 0.5f }; ///< Of a bin's room filled before a handyman empties it
        Seconds dry { 1200 };    ///< A full flower bed's water lasts
        float garden_reach = 60; ///< Metres, as the crow flies, a handyman goes to water a dry flower bed
    };

    /// Ride wear, mechanics and flow. To tune against saved rides' reliability and downtime.
    struct Wear
    {
        Share per_run { 0.004f };       ///< Reliability a run costs
        Chance breakdown { 0.05 };      ///< Of a run ending in a breakdown, for a ride with no reliability left; less as more is left
        Share breakdown_gloom { 0.1f }; ///< Happiness lost by those queueing when their ride breaks down
        Seconds repair { 20 };
        Seconds inspect { 10 };
        Share repair_gain { 0.02f };    ///< Reliability a repair wins back
        Share inspect_gain { 0.05f };   ///< And an inspection
        Share flow_smoothing { 0.25f }; ///< Weight of the newest departure in a ride's riders an hour
        /// Chosen by eye: a stall's or cash machine's time to a breakdown is rolled between these, counted
        /// down at full speed in a park with no guards.
        Seconds stall_shortest { 1800 };
        Seconds stall_longest { 5400 };
        /// Chosen by eye: guests one fully trained guard looks after, halving how fast stalls wear; with
        /// fewer guests, each guard counts as looking after this many. Untrained guards count a quarter.
        float guard_guests = 200;
    };

    /// How content and lazy staff are, and their pay and training.
    struct Staff
    {
        /// A month's wage in hundredths for each job, in StaffRole's order, at wage levels 0, 1 (as hired) and 2 (after a
        /// raise); paid a quarter each week. Levels 1 and 2 of handymen, mechanics, guards and entertainers are the wages
        /// the game's staff window shows. Chosen by eye: level 0, and vendors' and keepers' wages (keepers below guards).
        /// Lifeguards (Other) and park inspectors are not paid.
        std::array<std::array<std::int64_t, 3>, 8> wages { {
            { 3000, 3500, 4200 }, // Handyman
            { 5000, 5500, 6500 }, // Mechanic
            { 1200, 1500, 2000 }, // Guard
            { 0, 0, 0 },          // Other
            { 300, 400, 500 },    // Vendor
            { 1000, 1300, 1600 }, // Keeper
            { 0, 0, 0 },          // Inspector
            { 3400, 4000, 4800 }, // Entertainer
        } };
        Seconds load_window { 600 };                        ///< Over how long their share of time at work is judged
        Share content_base { 0.45f };                       ///< Happiness of an untrained worker on the lowest wage
        Share content_per_training { 0.1f };                ///< More for each level of training
        Share content_per_wage { 0.1f };                    ///< More for each wage level
        Share overworked { 0.85f };                         ///< Of time at work, past which they feel overworked
        Share bored { 0.15f };                              ///< Below which they feel idle
        float overwork_gloom = 2;                           ///< Happiness lost for each share past overworked
        float bored_gloom = 3;                              ///< And for each share below idle
        PerSecond mood = PerSecond::over({ 900 });          ///< How fast happiness goes to where it is heading
        PerSecond lonely_gloom = PerSecond::over({ 1800 }); ///< Chosen by eye: happiness an untrained entertainer loses with no guests near
        Share lazy_below { 0.5f };                          ///< Happiness below which laziness grows
        PerSecond lazy = PerSecond::over({ 600 });          ///< Laziness grown for each share below that, untrained
        PerSecond lazy_fade = PerSecond::over({ 3600 });
        Share lazy_after_warning { 0.3f };                                                       ///< Laziness at which a warning is deserved
        Share warning_gloom { 0.2f };                                                            ///< Happiness a warning not deserved takes away
        Frequency rest = Frequency::every({ 60 });                                               ///< A worker wholly lazy taking a rest
        std::array<Money, 3> training_cost { Money { 5000 }, Money { 10000 }, Money { 20000 } }; ///< For each level from untrained up
        Seconds think { 15 };                                                                    ///< Between a worker's thoughts, about as often as saved parks' staff think
        Share tired { 0.5f };                                                                    ///< Chosen by eye: laziness from which they think they are tired
        Share keen { 0.05f };                                                                    ///< Chosen by eye: laziness up to which they think they are keen
        Share glad { 0.85f };                                                                    ///< Chosen by eye: happiness from which they think they are happy
        Share miserable { 0.25f };                                                               ///< Chosen by eye: happiness below which they think they are miserable
        std::uint32_t quit_after = 20;                                                           ///< Chosen by eye: thoughts in a row miserable before they think of quitting
        std::uint32_t leave_after = 10;                                                          ///< Chosen by eye: more such thoughts, after that, before they quit
    };

    /// The park rating: each part's share of the 1000, and the shapes inside them.
    struct Rating
    {
        RatingPoints happiness_share { 500 };
        RatingPoints rides_share { 300 };
        RatingPoints reliability_share { 100 };
        RatingPoints guests_share { 300 };
        Share empty_park_happiness { 0.5f };
        RideScore good_excitement { 7 }; ///< A ride this exciting scores in full
        RideScore harsh_intensity { 7 }; ///< Above this, a ride starts to put guests off
        RideScore intensity_range { 4 }; ///< How far above harsh a ride stops scoring at all
        float rides_for_variety = 3;     ///< About two thirds of the variety at this many rides
        float guests_for_busy = 300;     ///< About two thirds of the guests part at this many guests
        std::int32_t per_litter = 3;
        std::int32_t most_litter = 500;
        Share harder_share { 0.15f }; ///< Taken off the score by the harder rating
        Share weekly_move { 0.5f };   ///< Of the way the rating moves towards the week's score
    };

    /// Boats that leave their dock for open water.
    struct Boats
    {
        Seconds trip { 75 };          ///< Out on the water before they turn for home
        Pace top_speed { 2.5f };      ///< Rowed or driven
        Distance clear_of_dock { 4 }; ///< Chosen by eye: how far out a boat gets before the next casts off
    };

    /// What the prizes for winning a scenario level give.
    struct Prizes
    {
        Money large_cash { 1'500'000 };
        Money small_cash { 250'000 };
        RatingPoints rating_bonus { 80 };     ///< Chosen by eye: added to the park's rating at once, and to each week's score
        RatingPoints rating_bonus_fade { 4 }; ///< Chosen by eye: taken off that bonus each week, so it is gone in five months
    };

    /// Trains crashing off the end of their track.
    struct Crashes
    {
        Seconds forgotten { 1800 }; ///< After a ride's last crash, the park forgets it
        Distance blast { 6 };       ///< Guests on foot this near a wreck blowing up are knocked over
        Distance seen { 40 };       ///< Guests this near a crash think it a disaster
        Seconds knocked { 5 };      ///< A guest knocked over lies for, then gets up
        Pace scatter { 2.5f };      ///< Thrown riders fly at most this much faster or slower than their car, each way
        Pace blown { 6 };           ///< A blast throws those on foot beside it outwards at most this fast
        Pace lofted { 5 };          ///< And up at most this fast
        float tumble = 4;           ///< Radians a second a wreck tumbles at most
    };

    /// People the player picks up and lets go of.
    struct Holding
    {
        Distance lowest { 3 };   ///< A held person hangs at least this far over the ground under the cursor
        Distance highest { 5 };  ///< and at most this far
        float gravity = 9.8f;    ///< World units a second a falling person speeds up by, each second
        Pace fastest_fall { 6 }; ///< Most they fall at
        Chance happy { 0.6 };    ///< Of a held person enjoying it; the rest hang there cross
    };

    /// People on foot whose ground the land tools tip too steep: they slide down it, fall over, and get up.
    struct Slides
    {
        float steep = 0.3f;    ///< Chosen by eye: rise over run of the ground past which those on it slide; a ground path tile tipped as far as it may go is past it
        float grip = 0.25f;    ///< Chosen by eye: the ground's rub on a slider, as a share of their weight
        Pace settled { 0.3f }; ///< Chosen by eye: a slider slower than this, on ground no steeper than `steep`, stops
        Seconds longest { 8 }; ///< Chosen by eye: the longest a slide lasts, however steep the ground goes on
    };

    /// Guests of honour (VIPs) judging the park on their visit; the tolerances and targets they are weighed
    /// against come from the scenario.
    struct Vips
    {
        Seconds look { 4 };     ///< Between their looks round the park
        Share cheer { 0.025f }; ///< Their patience grows by this share of their happiness at each look
        std::int32_t heap = 3;  ///< Pieces of litter by them from which on it counts in full
        Share few { 0.4f };     ///< What less litter than a heap counts for
        std::int32_t reach = 3; ///< Tiles round them they see a themed area in
    };

    /// Entertainers' shows and the crowds round them.
    struct Shows
    {
        Seconds look { 3 };                          ///< Chosen by eye: between an entertainer's looks for a crowd
        Distance reach { 6 };                        ///< Chosen by eye: how far off guests notice a show, or a VIP
        Distance near { 1 };                         ///< Chosen by eye: closer than this, a guest walks on by
        Distance ring { 2.4f };                      ///< Chosen by eye: how far from the entertainer the crowd stands
        Frequency join = Frequency::every({ 0.5f }); ///< Chosen by eye: a free guest in reach stopping to watch, shared by their party; soon, as shows are short
        Share keen { 0.3f };                         ///< Chosen by eye: of starting a show with guests near, untrained, content and not lazy
        Share keen_per_training { 0.15f };           ///< Chosen by eye: more for each level of training
        Share poor_lazy { 0.7f };                    ///< Chosen by eye: laziness from which a show is a poor one, as when miserable
        std::int32_t moves = 5;                      ///< Chosen by eye: moves in a show of an untrained entertainer, long enough to draw a crowd; each level adds one
        Seconds gap { 20 };                          ///< Chosen by eye: rest after a show before the next
        Share cheer { 0.08f };                       ///< Chosen by eye: happiness a good show gives each in the crowd
        Share poor_gloom { 0.03f };                  ///< Chosen by eye: happiness an entertainer loses over a poor show
        Seconds clap { 2.5f };                       ///< Chosen by eye: the crowd claps for
        /// How long each ShowMove plays, in its order: the lengths of the motions in the costumes' meshes, which
        /// every costume has alike.
        std::array<Seconds, 9> move_length { Seconds { 1.33f }, Seconds { 2.67f }, Seconds { 1.73f }, Seconds { 1.97f }, Seconds { 1.63f }, Seconds { 5.0f }, Seconds { 1.27f }, Seconds { 1.9f }, Seconds { 2.0f } };
    };

    /// Guests who want a VIP's autograph.
    struct Fans
    {
        Seconds poll { 4 };      ///< Chosen by eye: between a VIP's looks for fans
        Chance keen { 0.35 };    ///< Chosen by eye: of a free guest in reach wanting one, shared by their party
        Distance ring { 1.2f };  ///< Chosen by eye: how far from the VIP fans stand
        Distance arm { 1.6f };   ///< Chosen by eye: a fan this near is signed for
        Distance close { 4.5f }; ///< Chosen by eye: a fan this near stops to be signed, and the VIP stops for them
        Seconds give_up { 45 };  ///< Chosen by eye: a fan follows and waits this long, then gives up
        Distance far { 32 };     ///< Chosen by eye: a fan left this far behind gives up
        Share cheer { 0.12f };   ///< Chosen by eye: happiness an autograph gives
    };

    /// The park inspector's rounds.
    struct Inspector
    {
        Seconds gap { 20 };            ///< Chosen by eye: walking about between visits
        Seconds again { 600 };         ///< Chosen by eye: before they visit the same attraction again
        Seconds quiet { 1920 };        ///< Chosen by eye: before they tell the player the same of the same thing again
        Share crowd { 0.3f };          ///< Chosen by eye: of the guests, how many must want one thing for them to say so
        Distance mechanic_near { 40 }; ///< Chosen by eye: how near a broken ride a mechanic must be for them to say nothing
        Distance watch_near { 4 };     ///< Chosen by eye: how near a worker they stand to watch them
        Seconds watch { 15 };          ///< Chosen by eye: how long they watch
        Seconds chase { 120 };         ///< Chosen by eye: how long they go after a worker before giving up
        Share lazy { 0.6f };           ///< Chosen by eye: laziness past which they call a worker lazy
        Share unhappy { 0.3f };        ///< Chosen by eye: happiness under which they call a worker unhappy
        Share filthy { 0.8f };         ///< Chosen by eye: pool dirt from which they call it dangerous; dirty from pools.bad
        Share breakdowns { 0.2f };     ///< Chosen by eye: of the attractions that can break, how many broken at once for them to say so
    };

    /// Rides with more than one station as ways across the park: a guest going far may ride from one station
    /// to another when that is sooner than walking, counting the wait in its queue and the ride.
    struct Transport
    {
        Seconds refresh { 30 };  ///< Between fresh looks at each ride's queues, closures and breakdowns
        Seconds boarding { 30 }; ///< Through the station, waiting for a train and getting on and off
        Seconds dwell { 15 };    ///< A train stands at each station on the way
    };

    /// Swimming pools.
    struct Pools
    {
        PerSecond longing = PerSecond::over({ 1500 }); ///< Wish to swim grown in a guest who likes pools fully; slower as they like them less
        Share least_liking { 0.1f };                   ///< Guests like pools from this much to `most_liking`, as saved guests do
        Share most_liking { 0.9f };
        Degrees coldest { 18 };      ///< Under which guests will not swim
        Seconds stay { 300 };        ///< In the pool, out of their clothes: the longest saved swimmers have left
        Seconds changing { 20 };     ///< In the changing room, each way
        Pace swim { 0.7f };          ///< Across the water
        Seconds shortest_rest { 3 }; ///< Floating or standing between swims
        Seconds longest_rest { 15 };
        Chance poolside { 0.2 };                    ///< Of going to the paved squares next rather than the water
        Chance extra { 0.3 };                       ///< Of going to a spa pool or an extra next, of those that go on from the water
        Chance slide { 0.25 };                      ///< Chosen by eye: of going up one of the pool's slides next, when it has any
        std::uint8_t lounger_places = 2;            ///< Swimmers a set of sun loungers takes at once, as the saved parks' do
        std::uint8_t shower_places = 3;             ///< And a shower
        Seconds lounging { 45 };                    ///< Chosen by eye: on a lounger
        float deck = 0.2f;                          ///< The paving's top over the pool's surface, in metres, where saved guests walk it
        float lounger_reach = 1.44f;                ///< From a set of loungers' middle to where one lying on the last is placed, the way they face, in metres, as a saved park's are
        float lounger_side = 0.55f;                 ///< And sideways from there, as a saved park's are
        float lounger_gap = 1.75f;                  ///< Between the loungers of a set, the way those on them face, in metres, measured by eye on the model
        float shower_reach = 0.75f;                 ///< From a shower's pole to those under it, in metres, as a saved park's are
        Seconds showering { 6 };                    ///< Chosen by eye: under a shower
        Seconds patience { 20 };                    ///< Chosen by eye: waiting for a place at an extra before going elsewhere
        float spa_water = 0.0f;                     ///< A spa pool's water over the paving round it, in height units
        PerSecond cheer = PerSecond::over({ 600 }); ///< Happiness gained while swimming in clean water; lost in dirty
        /// Dirt a swimmer brings to a pool of one square of water in a second; less in bigger pools.
        PerSecond dirtying = PerSecond::over({ 1200 });
        /// Dirt a room's fluid takes out of its pool in a second, by fluid, while the tank has any.
        std::array<PerSecond, 3> cleaning { PerSecond::over({ 1500 }), PerSecond::over({ 700 }), PerSecond::over({ 300 }) };
        PerSecond tank_use = PerSecond::over({ 1800 });                                 ///< Of a full tank, while the room is open
        Share refill_at { 0.25f };                                                      ///< Left in the tank when a mechanic is called to fill it
        std::array<Money, 3> refill { Money { 2000 }, Money { 4000 }, Money { 8000 } }; ///< A full tank, by fluid
        Share good { 2.0f / 6 };                                                        ///< Dirt under which swimmers think the pool clean
        Share bad { 3.0f / 6 };                                                         ///< And from which dirty
    };

    /// Lazy rivers: swimmers float round them in rings, carried by the current.
    struct Rivers
    {
        Pace flow { 1.7f };         ///< The current, as far as the saved parks' riders went over their time on the river
        Pace least_paddle { 0.5f }; ///< Riders paddle from this fast to `most_paddle`, ahead or back, as saved riders do
        Pace most_paddle { 2.0f };
        float least_stay = 0.5f; ///< Laps a rider floats before getting off, from this many to `most_stay`, as saved riders mean to
        float most_stay = 2.0f;
        float spin = 1.5f;                        ///< Most radians a second a resting rider's ring turns, about as fast as saved riders' rings turn
        Chance paddle { 0.15 };                   ///< Chosen by eye: of paddling off, each time a resting rider's ring comes round to face along the river or back
        Frequency rest = Frequency::every({ 8 }); ///< Chosen by eye: how often a paddling rider stops to rest
        float lane = 1.0f;                        ///< Chosen by eye: most metres to either side of the channel's middle a rider floats
        float join = 2.0f;                        ///< Chosen by eye: tiles from a station's track to pool path stairs that join it to their complex
        /// Excitement, intensity and nausea of a river not rated yet, as a saved park rates its river (with no test run)
        std::array<float, 3> rated { 3.6f, 0.9f, 0.0f };
    };

    /// Dives off a pool's boards.
    struct Dives
    {
        Chance chance { 0.15 };       ///< Chosen by eye: of heading for one of the pool's boards next, when it has any
        Chance ladder { 0.5 };        ///< Chosen by eye: of climbing out of the water by a ladder, when the pool has any
        Chance step_off { 0.3 };      ///< Chosen by eye: of walking out on a lower board of a high one, at each passed
        Pace climb { 1.2f };          ///< Chosen by eye: up a high board's ladder
        Seconds jump { 1.0f };        ///< Chosen by eye: springing off a board's end
        Pace leap { 1.5f };           ///< Chosen by eye: on along the way they face, as they fall
        Seconds surfacing { 2.0f };   ///< Chosen by eye: coming back up
        Share high_cheer { 0.04f };   ///< Chosen by eye: happiness from a dive off a high board
        Share small_cheer { 0.015f }; ///< Chosen by eye: and off a small one
        float behind = 0.3f;          ///< Chosen by eye: from the pool's wall back to where swimmers step on a small board, in world units
        float small_reach = 3.5f;     ///< From the wall to where a diver stands at a small board's end, as far as its model reaches
        float small_board = 0.3f;     ///< A small board's top over the paving, as its model stands
        float ladder_foot = -0.15f;   ///< From a high board's square middle towards the water to a climber on its ladder: the rungs' back is 0.05 ahead of the middle, as the tower's model has them, and the climber's middle a fifth of a unit behind that, chosen by eye
        float ladder_side = 0.535f;   ///< And to the side, the middle of the rungs as the tower's model has them
        float ladder_queue = 0.8f;    ///< Chosen by eye: back from a high board's ladder foot to where the next swimmer waits their turn, clear of the tower
        float high_reach = 5.1f;      ///< From the ladder's climber out to where a diver stands at its boards' end, a little short of their tip as the model reaches
        float board_top = 1.2f;       ///< A high board's top over the height it is placed at, in world units, as its model stands
    };

    /// What guests make of the park round them, and the scenery they pass.
    struct Surroundings
    {
        Seconds look { 90 };             ///< Chosen by eye: between a guest's looks at what is round them
        std::int32_t reach = 2;          ///< Chosen by eye: tiles round a path tile whose scenery counts for it
        float plant = 1;                 ///< Chosen by eye: what a tree, shrub or flower bed adds
        float ornament = 1.5f;           ///< Chosen by eye: what other scenery adds
        float feature = 4;               ///< Chosen by eye: what a fountain, effect or animatronic adds
        float great = 20;                ///< Chosen by eye: scenery round a tile from which guests think it great
        Share great_cheer { 0.04f };     ///< Chosen by eye: happiness from thinking the scenery great
        Share tidy_cheer { 0.02f };      ///< Chosen by eye: happiness from thinking the park tidy
        Chance stop { 0.02 };            ///< Chosen by eye: of stopping to watch an effect or animatronic passed; most walk on
        Chance glance { 0.5 };           ///< Chosen by eye: of turning to look at jumping fountains passed
        Chance fountain_thought { 0.4 }; ///< Chosen by eye: of liking them, of those who look
        Seconds glancing { 1.2f };       ///< Chosen by eye: a look at them lasts
        Share fountain_cheer { 0.02f };  ///< Chosen by eye: happiness from liking them
        Chance fire_cannon { 0.2 };      ///< Chosen by eye: of taking a free water cannon passed
        Seconds firing { 30 };           ///< Chosen by eye: a guest fires one for
        Distance cannon_reach { 14 };    ///< Chosen by eye: how far off a cannon's firer picks someone to spray
        float cannon_swing = 1.75f;      ///< Chosen by eye: radians a cannon turns to either side of where it points at rest, a little past square to reach along its path
        Chance retarget { 0.25 };        ///< Chosen by eye: a second, of the firer swinging on to someone else
        Share soaking { 0.3f };          ///< Chosen by eye: wetness a second for someone the cannon sprays
        Share view_cheer { 0.05f };      ///< Chosen by eye: happiness from a fine view
        std::int32_t earshot = 2;        ///< Chosen by eye: tiles from a loudspeaker within which guests hear it
        Chance music_thought { 0.15 };   ///< Chosen by eye: of liking its music, on each tile walked in earshot
    };

    struct Tuning
    {
        Needs needs;
        Walking walking;
        Gaits gaits;
        Reactions reactions;
        Gestures gestures;
        Queues queues;
        Facilities facilities;
        Eating eating;
        Sickness sickness;
        Weather weather;
        Night night;
        Arrivals arrivals;
        Parties parties;
        Leaving leaving;
        Thoughts thoughts;
        Litter litter;
        Vandals vandals;
        Handymen handymen;
        Wear wear;
        Staff staff;
        Rating rating;
        Boats boats;
        Prizes prizes;
        Crashes crashes;
        Vips vips;
        Shows shows;
        Fans fans;
        Inspector inspector;
        Transport transport;
        Holding holding;
        Slides slides;
        Pools pools;
        Rivers rivers;
        Dives dives;
        Surroundings surroundings;
    };

} // namespace rct3::simulation::tuning

namespace rct3::simulation
{
    inline constexpr tuning::Tuning kTuning { };
} // namespace rct3::simulation
