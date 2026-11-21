// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every kind of component the simulation puts on its entities, and every park-wide value it keeps beside them, each
// with a four-letter tag of its own. A snapshot of the park writes each under its tag, and the fingerprint counts
// them by it, so neither depends on how a compiler names types.
//
// A new component or park-wide value goes in here; the park file test fails for a park that holds one missing.
#pragma once

#include "simulation/animal_care.hpp"
#include "simulation/animals.hpp"
#include "simulation/boat.hpp"
#include "simulation/components.hpp"
#include "simulation/crash.hpp"
#include "simulation/crowds.hpp"
#include "simulation/enjoyment.hpp"
#include "simulation/fence_wear.hpp"
#include "simulation/fireworks.hpp"
#include "simulation/guest_edits.hpp"
#include "simulation/guests.hpp"
#include "simulation/parties.hpp"
#include "simulation/ride_walks.hpp"
#include "simulation/rides.hpp"
#include "simulation/staff/patrol.hpp"
#include "simulation/staff/work.hpp"
#include "simulation/stalls.hpp"
#include "simulation/swimming.hpp"
#include "simulation/vip.hpp"
#include "simulation/wild_birds.hpp"

#include <array>
#include <string_view>
#include <tuple>
#include <vector>

namespace rct3::simulation
{

    /// A type of `T` under its tag.
    template<typename T> struct Tagged
    {
        using Type = T;
        std::array<char, 4> tag { };
        consteval explicit Tagged(const char (&text)[5])
            : tag { text[0], text[1], text[2], text[3] }
        {
        }
        /// The tag as a number, its first letter lowest.
        [[nodiscard]] constexpr u32 id() const
        {
            return static_cast<u32>(static_cast<u8>(tag[0])) | static_cast<u32>(static_cast<u8>(tag[1])) << 8 | static_cast<u32>(static_cast<u8>(tag[2])) << 16 | static_cast<u32>(static_cast<u8>(tag[3])) << 24;
        }
    };

    /// The components, in the order a snapshot writes them.
    inline constexpr auto kComponents = std::tuple {
        Tagged<Visitor> { "VSTR" },
        Tagged<Pose> { "POSE" },
        Tagged<Needs> { "NEED" },
        Tagged<Mood> { "MOOD" },
        Tagged<Wallet> { "WALT" },
        Tagged<Departure> { "DPRT" },
        Tagged<Upkeep> { "UPKP" },
        Tagged<staff::Patrol> { "PTRL" },
        Tagged<Gesture> { "GEST" },
        Tagged<Dice> { "DICE" },
        Tagged<Attraction> { "ATTR" },
        Tagged<Staff> { "STAF" },
        Tagged<Litter> { "LITR" },
        Tagged<Linger> { "LING" },
        Tagged<Swimmer> { "SWIM" },
        Tagged<Member> { "MEMB" },
        Tagged<Dropped> { "DROP" },
        Tagged<CycleRide> { "CYCL" },
        Tagged<AnimalCare> { "ACAR" },
        Tagged<Taste> { "TAST" },
        Tagged<Shop> { "SHOP" },
        Tagged<Party> { "PRTY" },
        Tagged<Umbrella> { "UMBR" },
        Tagged<Show> { "SHOW" },
        Tagged<staff::Rounds> { "RNDS" },
        Tagged<RideWalk> { "RWLK" },
        Tagged<Passport> { "PASS" },
        Tagged<ParkMap> { "PMAP" },
        Tagged<KeeperDuty> { "KDTY" },
        Tagged<HouseCare> { "HCAR" },
        Tagged<Favourite> { "FAVR" },
        Tagged<Dung> { "DUNG" },
        Tagged<AnimalHouse> { "AHSE" },
        Tagged<Wrecks> { "WRCK" },
        Tagged<Watching> { "WTCH" },
        Tagged<Thrill> { "THRL" },
        Tagged<StationNeighbours> { "STNB" },
        Tagged<PathItem> { "PITM" },
        Tagged<Outside> { "OUTS" },
        Tagged<Lost> { "LOST" },
        Tagged<Left> { "LEFT" },
        Tagged<KeeperTool> { "KTOL" },
        Tagged<Hunter> { "HUNT" },
        Tagged<Floating> { "FLOT" },
        Tagged<Carried> { "CARY" },
        Tagged<Bin> { "BIN " },
        Tagged<Animal> { "ANML" },
        Tagged<WildBird> { "BIRD" },
        Tagged<WalkThrough> { "WTHR" },
        Tagged<Vip> { "VIP " },
        Tagged<TrainStalls> { "TSTL" },
        Tagged<Trains> { "TRNS" },
        Tagged<Thoughts> { "THGT" },
        Tagged<TakingsRate> { "TAKE" },
        Tagged<SwimPool> { "SPOL" },
        Tagged<Stride> { "STRD" },
        Tagged<StampColour> { "STMP" },
        Tagged<Stage> { "STAG" },
        Tagged<RideWalks> { "RWKS" },
        Tagged<Ridden> { "RIDN" },
        Tagged<PoolTank> { "PTNK" },
        Tagged<PoolRoute> { "PRTE" },
        Tagged<PoolDoor> { "PDOR" },
        Tagged<ParkGate> { "GATE" },
        Tagged<HeldLevels> { "HLVL" },
        Tagged<Held> { "HELD" },
        Tagged<GuestSpawn> { "SPWN" },
        Tagged<Glance> { "GLNC" },
        Tagged<FlowerBed> { "FBED" },
        Tagged<FireworkWatcher> { "FWAT" },
        Tagged<Errand> { "ERND" },
        Tagged<Crossing> { "CROS" },
        Tagged<Coupon> { "CPON" },
        Tagged<ClimbSlide> { "CSLD" },
        Tagged<Climbing> { "CLMB" },
        Tagged<Carcass> { "CRCS" },
        Tagged<Bodyguard> { "BGRD" },
        Tagged<Boats> { "BOAT" },
        Tagged<Autographs> { "AUTO" },
        Tagged<River> { "RIVR" },
        Tagged<staff::Jobs> { "JOBS" },
        Tagged<staff::FenceMending> { "FMND" },
    };

    /// The park-wide values (the registry's context), in the order a snapshot writes them.
    inline constexpr auto kParkValues = std::tuple {
        Tagged<Daylight> { "DAYL" },  Tagged<Weather> { "WEAT" },     Tagged<Draw> { "DRAW" },          Tagged<Charges> { "CHRG" },       Tagged<Payroll> { "PAYR" },     Tagged<Supplies> { "SUPP" },      Tagged<Sales> { "SALE" },    Tagged<Receipts> { "RCPT" },     Tagged<Turnstile> { "TURN" }, Tagged<Findings> { "FIND" },    Tagged<Photos> { "PHOT" },        Tagged<PoolDangers> { "PDNG" },
        Tagged<Vandalism> { "VAND" }, Tagged<SceneryNear> { "SCNR" }, Tagged<EntrancePaths> { "ENTP" }, Tagged<PassportRules> { "PRUL" }, Tagged<CouponsUsed> { "CPNU" }, Tagged<RoomElsewhere> { "ROOM" }, Tagged<PartyMix> { "PMIX" }, Tagged<FireworkShow> { "FSHW" }, Tagged<WildBirds> { "BIRS" }, Tagged<FenceUpkeep> { "FUPK" }, Tagged<PartyBoarding> { "PBRD" }, Tagged<WaterTiles> { "WATR" },
    };

    /// Whether no two components or park-wide values share a tag, nor take one the park file keeps for itself.
    consteval bool tags_unique()
    {
        std::array<std::array<char, 4>, std::tuple_size_v<decltype(kComponents)> + std::tuple_size_v<decltype(kParkValues)> + 3> tags { { { 'W', 'R', 'L', 'D' }, { 'E', 'N', 'T', 'S' }, { 'P', 'L', 'N', 'K' } } };
        usize n = 3;
        std::apply([&](auto... t) { ((tags[n++] = t.tag), ...); }, kComponents);
        std::apply([&](auto... t) { ((tags[n++] = t.tag), ...); }, kParkValues);
        for (usize i = 0; i < tags.size(); ++i)
            for (usize j = i + 1; j < tags.size(); ++j)
                if (tags[i] == tags[j])
                    return false;
        return true;
    }
    static_assert(tags_unique());

    /// The names of the components `registry` holds that kComponents does not list, so a snapshot would lose them.
    [[nodiscard]] inline std::vector<std::string_view> untagged_components(const entt::registry& registry)
    {
        std::vector<std::string_view> out;
        for (const auto [id, storage] : registry.storage())
        {
            const bool listed = id == entt::type_hash<entt::entity>::value() || std::apply([&](auto... t) { return ((id == entt::type_hash<typename decltype(t)::Type>::value()) || ...); }, kComponents);
            if (!listed && !storage.empty())
                out.push_back(storage.info().name());
        }
        return out;
    }

} // namespace rct3::simulation
