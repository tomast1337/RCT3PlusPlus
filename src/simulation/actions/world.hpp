// SPDX-License-Identifier: GPL-3.0-or-later
//
// What an action works on, and what comes of it. Each kind of action is checked first (can it be done, and what it
// costs) and only then applied; one refused changes nothing.
#pragma once

#include "core/types.hpp"
#include "park/carried_goods.hpp"
#include "park/person/outfit.hpp"
#include "park/pools/pools.hpp"
#include "park/track/course.hpp"
#include "park/track/rules.hpp"
#include "save/model/park.hpp"
#include "save/model/people.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"
#include "simulation/day_clock.hpp"
#include "simulation/park/state.hpp"
#include "simulation/research.hpp"
#include "simulation/sim_world.hpp"
#include "units/units.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rct3::simulation::actions
{

    /// What applied actions changed of what the park is drawn from, for whoever draws it to build again only that,
    /// and then to empty, keeping the lists' memory. Lists are in the order the changes were made.
    struct Changed
    {
        /// Workers sacked, each by their place in the park's staff as it was when they went: a later one counts
        /// places after the earlier have gone.
        std::vector<usize> sacked_staff { };
        std::vector<entt::entity> hired_staff { };                  ///< Workers hired, by the walker made for them
        std::vector<save::model::PeopleObjectId> dressed_staff { }; ///< Workers whose clothes or their colours changed
        std::vector<u64> painted_objects { };                       ///< Placed objects given new colours, by id
        std::vector<save::model::ObjectId> fenced_rides { };        ///< Rides given another fence: it, their doors' paint and their queues' railings
        std::vector<save::model::ObjectId> redoored_rides { };      ///< Rides whose entrances or exits are shown as another item, and the fence round them
        bool land_owned = false;                                    ///< What the park owns changed: its boundary fence, and all objects placed again
        bool fences_worn = false;                                   ///< Enclosure fence panels were worn or broken: all objects placed again
        bool invented = false;                                      ///< Research finished at once: what may be built is listed again
    };

    /// The parts of a park that actions read and change. Whoever runs the park owns them and lends them for the call.
    struct World
    {
        SimWorld& sim;
        park::ParkState& park;
        save::model::ParkModel& model;
        save::model::RideModel& rides;
        Research& research;
        std::optional<std::vector<ResearchItem>>& research_items; ///< What can be researched; none until the build lists are read
        std::optional<i64>& ledger_month;                         ///< As park::ledger_month keeps it
        DayClock& day_clock;
        const rct3::park::CarriedKinds& carried;                                   ///< What stalls sell and its extras
        std::vector<save::model::StallIndex>& unjoined_stalls;                     ///< Stalls with something to sell, waiting to be joined to the paths
        const std::vector<rct3::park::track::TrackRules>& track_rules;             ///< By ride, what its kind asks of its track
        const std::vector<std::optional<rct3::park::track::TrackCourse>>& courses; ///< By ride, of those whose track is known whole
        const rct3::park::pools::PoolMap& pools;
        const std::optional<save::model::RideIndex>& building; ///< The ride under construction, if any
        std::vector<save::model::RideIndex>& readying;         ///< Tracked rides whose trains are to be made ready to run
        std::vector<save::model::RideShow>& shows;             ///< What the show rides put on, by ride id
        save::model::PeopleModel& people;
        std::vector<entt::entity>& staff_walkers;                ///< By place in `people.staff`, the simulated worker of each; null for one not walked
        const std::vector<rct3::park::person::Outfit>& wardrobe; ///< What workers hired may wear
        usize& costumes_given;                                   ///< To entertainers hired, each the next costume in turn
        const bool& simulating;                                  ///< Whether the park runs its simulation; none is hired without
        std::vector<save::model::PlacedObject>& objects;         ///< The park's placed objects: scenery, the rides' and stalls' parts
        Changed& changed;
        save::model::TerrainGrid& grid;        ///< The ground, and who owns each tile
        save::model::EnclosureMap& enclosures; ///< The animal enclosures' tiles and fences
    };

    /// Why an action was not done.
    struct Refusal
    {
        enum Kind : u8
        {
            Missing,      ///< It names something the park does not have, or no longer has
            CannotAfford, ///< The park has not the money
            NothingToDo,  ///< The park is already as asked, or can move no further that way
            Forbidden,    ///< The park's rules do not let it be done as things are
        };

        Refusal(Kind kind, std::string told = { })
            : kind(kind)
            , told(std::move(told))
        {
        }

        Kind kind;
        std::string told; ///< The string key the player is told why by, where the game tells them

        bool operator==(Kind other) const
        {
            return kind == other;
        }
    };

    /// An action that may be done.
    struct Checked
    {
        units::Money cost { }; ///< What doing it takes from the park's cash
    };

    using Outcome = std::expected<Checked, Refusal>;

} // namespace rct3::simulation::actions
