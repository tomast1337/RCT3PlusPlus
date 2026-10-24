// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/awards.hpp"

#include "strings/ascii.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <utility>

namespace rct3::simulation
{

    namespace
    {
        constexpr std::array<std::string_view, kAwardKinds> kNames = {
            "Award_Tidiest",      "Award_Most_Untidy",    "Award_Worst_Value",     "Award_Best_Value",     "Award_Best_Food",    "Award_Worst_Food",      "Award_Most_Thrilling", "Award_Most_Disappointing",   "Award_Most_Beautiful",      "Award_Safest",       "Award_Best_Staff",        "Award_Best_Toilets", "Award_Best_Roller_Coasters", "Award_Best_Water_Rides",       "Award_Best_Reliability", "Award_Worst_Reliability", "Award_Theme_Space",
            "Award_Theme_Spooky", "Award_Theme_WildWest", "Award_Theme_Adventure", "Award_Theme_Atlantis", "Award_Theme_Island", "Award_Most_Unsanitary", "Award_Most_Dangerous", "Award_Most_Dangerous_Pools", "Award_Extremely_Dangerous", "Award_Theme_Jungle", "Award_Theme_Prehistoric", "Award_Best_Animals", "Award_Cutest_Animals",       "Award_Most_Neglected_Animals",
        };

        constexpr std::array<std::int32_t, 10> kFines = { 1, 2, 5, 7, 15, 22, 23, 24, 25, 30 };
    } // namespace

    std::string_view award_name(std::int32_t kind)
    {
        return kind >= 0 && std::cmp_less(kind, kNames.size()) ? kNames[static_cast<std::size_t>(kind)] : std::string_view { };
    }

    bool award_is_fine(std::int32_t kind)
    {
        return std::ranges::contains(kFines, kind);
    }

    std::string_view award_message(std::int32_t kind)
    {
        return award_is_fine(kind) ? "Award_Fine" : "Award_Award";
    }

    bool award_earned(std::int32_t kind, const AwardFigures& f, std::uint32_t lately, random::RandomState& dice)
    {
        const auto had = [&](std::initializer_list<std::int32_t> kinds) { return std::ranges::any_of(kinds, [&](std::int32_t k) { return (lately & (1u << k)) != 0; }); };
        const auto count = [&](std::int32_t thought) {
            const auto found = f.thoughts.find(thought);
            return found != f.thoughts.end() ? found->second : 0;
        };
        const auto share = [&](std::int32_t thought) { return f.guests > 0 ? static_cast<float>(count(thought)) / static_cast<float>(f.guests) : 0.0f; };
        const auto rand = [&] { return static_cast<float>(random::unit(dice)); };
        // Litter, vandalism and sick lying about, as guests see it.
        const std::int32_t litter = count(3) + count(14) + count(15);
        const float litter_share = share(3) + share(14) + share(15);
        const auto rides = static_cast<std::int32_t>(f.downtime.size());
        const auto down_over = [&](float percent) { return static_cast<std::int32_t>(std::ranges::count_if(f.downtime, [&](float d) { return d > percent; })); };
        switch (kind)
        {
            case 0: // Tidiest
                return !had({ 1, 7 }) && f.guests >= 200 && static_cast<float>(f.guests) >= rand() * 500 && f.guests >= 10 * f.old_litter && share(6) > 1.0f / 64 && litter <= 5;
            case 1: // Most untidy
                return !had({ 0, 8, 10 }) && litter_share >= 1.0f / 16;
            case 2: // Worst value
                return !had({ 3 }) && f.fee_charged && f.entrance_fee > f.fair_fee;
            case 3: // Best value
                return !had({ 2, 7 }) && f.fee_charged && f.fair_fee >= 100 && static_cast<float>(f.entrance_fee + 10) < static_cast<float>(f.fair_fee) * 0.5f;
            case 4: // Best food
                return !had({ 5 }) && share(4) <= 0.25f && f.food_stalls >= 7 && f.food_kinds >= 4 && f.stalls * 128 >= f.guests;
            case 5: // Worst food
                return !had({ 4 }) && f.food_kinds <= 4 && f.stalls * 128 <= f.guests && share(4) > 0.3f;
            case 6: // Most thrilling
                return !had({ 7 }) && f.thrilling >= 6;
            case 7: // Most disappointing
            {
                float let_down = 0;
                for (const std::int32_t thought : { 182, 105, 27, 40, 44, 45, 180, 14 })
                    let_down += share(thought);
                return !had({ 3 }) && f.rating <= 500 && let_down > 0.25f;
            }
            case 8: // Most beautiful
                return !had({ 1, 7 }) && share(8) > 1.0f / 128 && litter <= 15;
            case 9: // Safest
            {
                constexpr float kGuests = 400;       ///< The crowd that makes a safe park news, at most
                constexpr std::int32_t kRides = 7;   ///< At least that many rides
                constexpr float kManyRides = 15;     ///< Past this a park is always big enough
                constexpr std::int32_t kSick = 5;    ///< Guests feeling sick, at most
                constexpr std::int32_t kDangers = 1; ///< Dangerous happenings, at most
                constexpr float kReliability = 72;   ///< Average reliability, more than
                return !had({ 22, 23, 24, 25 }) && static_cast<float>(f.guests) >= rand() * kGuests && rides >= kRides && static_cast<float>(rides) >= rand() * kManyRides && count(14) <= kSick && f.coaster_dangers + f.pool_dangers <= kDangers && f.crashed == 0 && f.reliability > kReliability;
            }
            case 10: // Best staff
                return !had({ 1 }) && litter <= 10 && f.every_staff_kind && f.staff * 32 > f.guests && down_over(25) == 0;
            case 11: // Best toilets
                return f.toilets >= 4 && f.toilets * 128 >= f.guests && share(12) < 0.25f;
            case 12: // Best roller coasters
                return !had({ 7 }) && f.coasters >= 6;
            case 13: // Best water rides
                return f.water_rides >= 6;
            case 14: // Best reliability
            {
                constexpr std::int32_t kRunning = 7; ///< Rides running, at least
                constexpr float kManyRunning = 25;   ///< Past this a park always has enough
                constexpr float kDown = 10;          ///< No ride out of action more of the time
                constexpr float kReliability = 78;   ///< Average reliability, more than
                return !had({ 15 }) && f.running >= kRunning && static_cast<float>(f.running) >= rand() * kManyRunning && down_over(kDown) == 0 && f.reliability > kReliability;
            }
            case 15: // Worst reliability
                return !had({ 14 }) && rides >= 3 && static_cast<float>(rides) >= rand() * 10 && (down_over(75) > 2 || down_over(30) > 6 || f.reliability < 40);
            case 16: // Themes, each named after its theme
            case 17:
            case 18:
            case 19:
            case 20:
            case 21:
            case 26:
            case 27:
            {
                constexpr std::int32_t kThemed = 5; ///< Fitted: rides and stalls carrying the theme, at least
                constexpr std::string_view kTheme = "Award_Theme_";
                const auto found = f.themed.find(award_name(kind).substr(kTheme.size()));
                return found != f.themed.end() && found->second >= kThemed;
            }
            case 22: // Most unsanitary
                return count(180) > 15;
            case 23: // Most dangerous
            case 25: // Extremely dangerous
            {
                constexpr std::int32_t kCoasterWeight = 5; ///< A coaster happening counts as this many at pools
                constexpr std::int32_t kDangerous = 25;    ///< Weighted happenings, at least
                constexpr std::int32_t kExtremely = 60;
                return f.coaster_dangers * kCoasterWeight + f.pool_dangers >= (kind == 23 ? kDangerous : kExtremely);
            }
            case 24: // Most dangerous pools
            {
                constexpr std::int32_t kPools = 12; ///< Happenings at pools, at least
                return f.pool_dangers >= kPools;
            }
            default:
                // shortcut: scenery themes and animals are never given, as what they count is not simulated; add them
                // as it is.
                return false;
        }
    }

    std::map<std::string, std::int32_t, std::less<>> themed_attractions(const save::RideModel& rides, std::span<const save::PlacedObject> objects, std::span<const park::BuildItem> catalogue)
    {
        constexpr float kReach = 6;   ///< Fitted: tiles round a ride or stall its scenery counts within
        constexpr float kOwn = 8;     ///< Fitted: a ride or stall of the theme counts as this many tiles of its scenery
        constexpr float kEnough = 96; ///< Fitted: tiles of the theme's scenery, its own counted in, that carry it
        // Overlays in one spelling: either slash, any case.
        const auto key = [](std::string_view s) {
            std::string out(s);
            for (char& c : out)
                c = c == '\\' ? '/' : strings::ascii_lower(c);
            return out;
        };
        std::map<std::string, std::string, std::less<>> theme_of;
        std::map<std::string, float, std::less<>> tiles_of; // Footprint of each item, by overlay and symbol
        for (const park::BuildItem& item : catalogue)
        {
            if (item.style_index > 0 && !item.style.empty())
                theme_of.try_emplace(key(item.overlay), item.style);
            tiles_of.try_emplace(key(item.overlay) + '|' + key(item.symbol), static_cast<float>(std::max(1u, item.width * item.length)));
        }
        struct Spot
        {
            float col = 0, row = 0;
            std::string_view theme;
            save::ObjectId object = 0;
        };
        std::vector<Spot> attractions;
        const auto add = [&](const save::AttractionCommon& base, std::span<const save::Station> stations) {
            std::optional<save::PlacementSpot> spot = base.spot;
            // Rides with a track stand where guests go in.
            for (const save::Station& station : stations)
                if (!spot && station.entrance_spot)
                    spot = station.entrance_spot;
            if (!spot)
                return;
            const auto theme = theme_of.find(key(base.overlay_path));
            attractions.push_back({ static_cast<float>(spot->col), static_cast<float>(spot->row), theme != theme_of.end() ? std::string_view(theme->second) : std::string_view { }, spot->object });
        };
        for (const save::Ride& ride : rides.rides)
            add(ride.base, ride.stations);
        for (const save::Stall& stall : rides.stalls)
            add(stall.base, { });
        // Each ride and stall's tiles of each theme's scenery round it, its own theme first.
        std::vector<std::map<std::string_view, float>> tiles(attractions.size());
        for (std::size_t i = 0; i < attractions.size(); ++i)
            if (!attractions[i].theme.empty())
                tiles[i][attractions[i].theme] = kOwn;
        for (const save::PlacedObject& o : objects)
        {
            const auto theme = o.hidden || o.fixture ? theme_of.end() : theme_of.find(key(o.overlay));
            if (theme == theme_of.end() || std::ranges::contains(attractions, o.id, &Spot::object))
                continue;
            const auto size = tiles_of.find(key(o.overlay) + '|' + key(std::string_view(o.symbol).substr(0, o.symbol.find(':'))));
            for (std::size_t i = 0; i < attractions.size(); ++i)
                if (std::hypot(static_cast<float>(o.col) - attractions[i].col, static_cast<float>(o.row) - attractions[i].row) <= kReach)
                    tiles[i][theme->second] += size != tiles_of.end() ? size->second : 1;
        }
        std::map<std::string, std::int32_t, std::less<>> themed;
        for (const auto& round : tiles)
            for (const auto& [theme, n] : round)
                if (n >= kEnough)
                    ++themed[std::string(theme)];
        return themed;
    }

    std::vector<std::int32_t> award_month(save::Awards& awards, const AwardFigures& figures, random::RandomState& dice)
    {
        if (awards.months_before_start > 0)
        {
            --awards.months_before_start;
            return { };
        }
        // The last six months, this one first; what the five before it had keeps those away.
        constexpr std::size_t kMonthsKept = 6;
        awards.earned.insert(awards.earned.begin(), std::vector<std::int32_t> { });
        awards.earned.resize(kMonthsKept);
        std::uint32_t lately = 0;
        for (std::size_t m = 1; m < awards.earned.size(); ++m)
            for (const std::int32_t k : awards.earned[m])
                if (k >= 0 && k < kAwardKinds)
                    lately |= 1u << k;
        std::vector<std::int32_t> given;
        for (std::int32_t kind = 0; kind < kAwardKinds; ++kind)
        {
            if ((lately & (1u << kind)) != 0 || (awards.available_mask & (1u << kind)) == 0 || !award_earned(kind, figures, lately, dice))
                continue;
            awards.earned.front().push_back(kind);
            lately |= 1u << kind;
            given.push_back(kind);
        }
        // Dangerous events are forgotten in time, those at pools sooner.
        if (awards.dangerous_pool_events && *awards.dangerous_pool_events > 0 && random::chance(dice, 0.5))
            --*awards.dangerous_pool_events;
        if (awards.dangerous_coaster_events && *awards.dangerous_coaster_events > 0 && random::chance(dice, 0.05))
            --*awards.dangerous_coaster_events;
        return given;
    }

} // namespace rct3::simulation
