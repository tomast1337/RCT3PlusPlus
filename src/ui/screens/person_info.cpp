// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/person_info.hpp"

#include "strings/unicode.hpp"
#include "ui/money_text.hpp"
#include "ui/thought_text.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <utility>

namespace rct3::ui
{

    namespace
    {
        constexpr std::array<PersonTabInfo, 6> kGuestTabs { {
            { PersonTab::General, 0, "Guest_Options", "GuestDetails_ViewHelp" },
            { PersonTab::Thoughts, 1, "Guest_Thoughts", "GuestDetails_ThoughtsHelp" },
            { PersonTab::Rides, 2, "Guest_Rides", "GuestDetails_RidesVisitedHelp" },
            { PersonTab::Carrying, 3, "Guest_Carrying", "GuestDetails_CarryingHelp" },
            { PersonTab::Stats, 4, "Guest_Stats", "GuestDetails_StatsHelp" },
            { PersonTab::Cash, 5, "Guest_Cash", "GuestDetails_CashHelp" },
        } };
        constexpr std::array<PersonTabInfo, 3> kStaffTabs { {
            { PersonTab::General, 0, "Staff_InfoIcon", "Staff_Details" },
            { PersonTab::Thoughts, 1, "Guest_Thoughts", "Staff_Thoughts" },
            { PersonTab::Stats, 2, "Guest_Stats", "Staff_Stats" },
        } };

        /// The string of each kind of staff's job, in the order of save::StaffKind.
        constexpr std::array<std::string_view, 8> kJobNames = {
            "HandymanName", "MechanicName", "SecurityGuardName", "EntertainerName", "VendorName", "AnimalKeeperName", "LifeguardName", "ParkInspectorName",
        };
        constexpr std::string_view kJobMark = "%ME%"; ///< Where a staff name has its job's

        /// Words for a guest's liking of intense rides and their stomach, gentlest first.
        constexpr std::array<std::string_view, 4> kIntensityWords = { "PersonEditor_Gentle", "PersonEditor_Normal", "PersonEditor_Wild", "PersonEditor_Crazy" };
        constexpr std::string_view kToleranceWord = "RideRating";

        /// Lines of rides gone on and of thoughts at most: as many as the page has room for.
        constexpr std::size_t kListLines = 6;
        constexpr float kPercent = 100;
        constexpr int kMinutesPerHour = 60;

        std::u16string ascii(std::string_view s)
        {
            return { s.begin(), s.end() };
        }

        std::u16string text(const StringTable& strings, std::string_view key)
        {
            return std::u16string(strings.get(key));
        }

        void add(InfoPage& page, const StringTable& strings, std::string_view label, std::u16string value)
        {
            page.lines.push_back({ text(strings, label), std::move(value) });
        }

        /// A line that shares its row with the one beside it.
        void add_half(InfoPage& page, const StringTable& strings, std::string_view label, std::u16string value)
        {
            page.lines.push_back({ text(strings, label), std::move(value), true });
        }

        std::u16string percent(float fraction)
        {
            return ascii(std::format("{} %", std::lround(std::clamp(fraction, 0.0f, 1.0f) * kPercent)));
        }

        std::u16string count(std::int32_t n)
        {
            return ascii(std::to_string(n));
        }

        /// The attraction with the id `id`, if the park has it.
        const save::AttractionCommon* attraction(const PersonFacts& f, save::PeopleObjectId id)
        {
            if (f.rides == nullptr || id == 0)
                return nullptr;
            for (const save::Ride& r : f.rides->rides)
            {
                if (r.base.id == id)
                    return &r.base;
            }
            for (const save::Stall& s : f.rides->stalls)
            {
                if (s.base.id == id)
                    return &s.base;
            }
            return nullptr;
        }

        std::u16string attraction_called(const PersonFacts& f, save::PeopleObjectId id, const StringTable& strings)
        {
            const save::AttractionCommon* a = attraction(f, id);
            return a != nullptr ? attraction_name(*a, strings) : std::u16string { };
        }

        std::u16string job(const save::Staff& s, const StringTable& strings)
        {
            const auto kind = static_cast<std::size_t>(s.kind);
            return kind < kJobNames.size() ? text(strings, kJobNames[kind]) : std::u16string { };
        }

        std::string_view state_key(const save::Person& p, bool guest)
        {
            using enum save::PersonState;
            switch (p.state)
            {
                case Decision:
                    return "PersonState_Decision";
                case Dangling:
                    return "PersonState_Dangling";
                case FallToGround:
                    return "PersonState_FallToGround";
                case Drowning:
                    return "PersonState_Drowning";
                case Wandering:
                    return guest ? "PersonState_Wandering" : "PersonState_Patrol";
                case SelectingPosition:
                    return "PersonState_PosSelect";
                case Queueing:
                    return "PersonState_Queueing";
                case OnRide:
                    return "PersonState_OnRide";
                case ExitingRide:
                    return "PersonState_ExitingRide";
                case AtAttraction:
                    return "PersonState_UsingAttraction";
                case HeadingFor:
                    return "PersonState_HeadingFor";
                case Lost:
                    return "PersonState_Lost";
                case Rescued:
                    return "PersonState_Rescued";
                case Swimming:
                    return "PersonState_Swimming";
                case HighDive:
                    return "PersonState_HighDivingBoard";
                case Jacuzzi:
                    return "PersonState_Jacuzzi";
                case Fleeing:
                    return "PersonState_Fleeing";
                case Passport:
                    return "PersonState_GettingPassport";
            }
            if (!guest)
                return { };
            const auto own = p.guest_state();
            if (!own)
                return { };
            using enum save::GuestState;
            switch (*own)
            {
                case HeadingForBench:
                    return "PersonState_HeadingForBench";
                case WalkingToPark:
                    return "PersonState_WalkingToPark";
                case WalkingHome:
                    return "PersonState_WalkingHome";
                case HeadingForBin:
                    return "PersonState_HeadingForBin";
                case FollowingGroup:
                    return "PersonState_FollowingGroup";
                case AdmiringView:
                    return "PersonState_AdmiringView";
                case GettingAutograph:
                    return "PersonState_GettingAutograph";
                case WatchingFireworks:
                    return "PersonState_WatchingFireworks";
                case WatchingEntertainer:
                    return "PersonState_WatchingEntertainer";
            }
            return { };
        }

        std::u16string time_in_park(std::int32_t minutes)
        {
            const std::int32_t m = std::max(minutes, 0);
            return ascii(std::format("{}:{:02}", m / kMinutesPerHour, m % kMinutesPerHour));
        }

        void thoughts(InfoPage& page, const PersonFacts& f, const StringTable& strings)
        {
            std::vector<const save::Thought*> latest;
            for (const save::Thought& t : f.person()->thoughts)
            {
                if (!t.empty())
                    latest.push_back(&t);
            }
            std::ranges::stable_sort(latest, std::ranges::greater { }, &save::Thought::time);
            for (const save::Thought* t : latest)
            {
                if (page.lines.size() == kListLines)
                    break;
                if (std::u16string said = thought_text(strings, t->id, t->variant, attraction_called(f, t->about.id, strings)); !said.empty())
                    page.lines.push_back({ std::move(said), { } });
            }
        }

        void rides(InfoPage& page, const save::Guest& g, const PersonFacts& f, const StringTable& strings)
        {
            std::u16string favourite = attraction_called(f, g.favourite_ride.id, strings);
            add(page, strings, "Guest_FavouriteRide", favourite.empty() ? text(strings, "Guest_NoFavouriteRide") : std::move(favourite));
            add(page, strings, "Guest_RidesBeenOn", count(g.counters.rides));
            // The latest first, each once.
            std::vector<save::PeopleObjectId> listed;
            for (auto it = g.person.attractions_visited.rbegin(); it != g.person.attractions_visited.rend() && page.lines.size() < kListLines; ++it)
            {
                if (std::ranges::contains(listed, it->id))
                    continue;
                listed.push_back(it->id);
                if (std::u16string name = attraction_called(f, it->id, strings); !name.empty())
                    page.lines.push_back({ std::move(name), { } });
            }
        }

        std::u16string item_called(const PersonFacts& f, const save::PeopleRef& ref, const StringTable& strings)
        {
            if (f.people == nullptr || ref.is_none())
                return { };
            const auto item = std::ranges::find(f.people->carried_items, ref.id, &save::CarriedItem::id);
            if (item == f.people->carried_items.end())
                return { };
            if (const auto found = strings.get(item->name); !found.empty())
                return std::u16string(found);
            return strings::to_utf16(item->name);
        }

        void carrying(InfoPage& page, const save::Guest& g, const PersonFacts& f, const StringTable& strings)
        {
            const save::ItemHolder* holder = nullptr;
            if (f.people != nullptr && !g.person.holder.is_none())
            {
                const auto it = std::ranges::find(f.people->holders, g.person.holder.id, &save::ItemHolder::id);
                if (it != f.people->holders.end())
                    holder = &*it;
            }
            const std::array<std::pair<std::string_view, save::PeopleRef>, 5> slots { {
                { "Guest_HolderHead", holder ? holder->head : save::PeopleRef { } },
                { "Guest_HolderLHand", holder ? holder->left_hand : save::PeopleRef { } },
                { "Guest_HolderRHand", holder ? holder->right_hand : save::PeopleRef { } },
                { "Guest_HolderLPocket", holder ? holder->left_pocket : save::PeopleRef { } },
                { "Guest_HolderRPocket", holder ? holder->right_pocket : save::PeopleRef { } },
            } };
            for (const auto& [label, ref] : slots)
            {
                std::u16string name = item_called(f, ref, strings);
                add(page, strings, label, name.empty() ? text(strings, "Guest_CarryingNothing") : std::move(name));
            }
        }

        void guest_stats(InfoPage& page, const save::Guest& g, const StringTable& strings)
        {
            add_half(page, strings, "Guest_Happiness", percent(g.mood_now.happiness));
            add_half(page, strings, "Guest_Energy", percent(g.mood_now.energy));
            add_half(page, strings, "Guest_Hunger", percent(1 - g.needs.food));
            add_half(page, strings, "Guest_Thirst", percent(1 - g.needs.drink));
            add_half(page, strings, "Guest_Toilet", percent(g.needs.toilet));
            add_half(page, strings, "Guest_Nausea", percent(g.mood_now.nausea));
            if (g.mood_now.sunburn)
                add_half(page, strings, "Guest_SunBurn", percent(*g.mood_now.sunburn));
        }

        void guest_general(InfoPage& page, const save::Guest& g, const StringTable& strings)
        {
            const auto liking = static_cast<std::size_t>(g.ride_preference.intensity);
            if (liking < kIntensityWords.size())
                add(page, strings, "Guest_PreferredRideIntensity", text(strings, kIntensityWords[liking]));
            add(page, strings, "Guest_NauseaTolerance", text(strings, std::format("{}{}", kToleranceWord, static_cast<int>(g.nausea_tolerance))));
            add(page, strings, "Guest_TimeInPark", time_in_park(g.counters.minutes_in_park));
        }

        void staff_stats(InfoPage& page, const save::Staff& s, const StringTable& strings)
        {
            add(page, strings, "Guest_Happiness", percent(s.happiness));
            add(page, strings, "StaffWindow_LazinessColumnHelp", percent(s.laziness));
            if (const auto* w = std::get_if<save::HandymanWork>(&s.work))
            {
                add(page, strings, "Handyman_PathsSwept", count(w->paths_swept));
                add(page, strings, "Handyman_GardensWatered", count(w->gardens_watered));
                add(page, strings, "Handyman_BinsEmptied", count(w->bins_emptied));
                add(page, strings, "Handyman_GrassMown", count(w->grass_mown));
            }
            else if (const auto* m = std::get_if<save::MechanicWork>(&s.work))
            {
                add(page, strings, "Mechanic_RidesInspected", count(m->objects_inspected));
                add(page, strings, "Mechanic_RidesFixed", count(m->objects_fixed));
            }
            else if (const auto* k = std::get_if<save::KeeperWork>(&s.work))
            {
                add(page, strings, "AnimalKeeper_DungCleared", count(k->dung_cleared));
                add(page, strings, "AnimalKeeper_TroughsFilled", count(k->troughs_filled));
            }
        }

        void cash(InfoPage& page, const save::Guest& g, const StringTable& strings, const InfoUnits& units)
        {
            const save::Cash& c = g.cash;
            const auto money = [&](std::int32_t amount) { return money_text(units.currency, amount); };
            add(page, strings, "Guest_CashInPocket", money(c.current));
            add(page, strings, "Guest_CashSpent", money(c.total_spent));
            // What the money went on, two to a row.
            add_half(page, strings, "Guest_SpentOnRides", money(c.spent_rides));
            add_half(page, strings, "Guest_SpentOnFood", money(c.spent_food));
            add_half(page, strings, "Guest_SpentOnDrink", money(c.spent_drink));
            add_half(page, strings, "Guest_SpentOnSouvenirs", money(c.spent_souvenirs));
            add(page, strings, "Guest_PaidToEnterPark", money(c.spent_park_entry));
            add(page, strings, "Guest_CashWithdrawn", money(c.withdrawn));
        }

    } // namespace

    std::vector<PersonTabInfo> person_tabs(bool staff)
    {
        if (staff)
            return { kStaffTabs.begin(), kStaffTabs.end() };
        return { kGuestTabs.begin(), kGuestTabs.end() };
    }

    std::u16string person_name(const PersonFacts& facts, const StringTable& strings)
    {
        const save::Person* p = facts.person();
        if (p == nullptr)
            return { };
        const std::u16string work = facts.staff != nullptr ? job(*facts.staff, strings) : std::u16string { };
        const std::string_view name = p->name;
        if (const auto mark = name.find(kJobMark); mark != std::string_view::npos)
            return strings::to_utf16(name.substr(0, mark)) + work + strings::to_utf16(name.substr(mark + kJobMark.size()));
        if (name.empty())
            return work;
        return strings::to_utf16(name);
    }

    std::u16string person_doing(const PersonFacts& facts, const StringTable& strings)
    {
        const save::Person* p = facts.person();
        if (p == nullptr)
            return { };
        const std::string_view key = state_key(*p, facts.guest != nullptr);
        if (key.empty())
            return { };
        std::u16string doing = text(strings, key);
        if (p->state == save::PersonState::HeadingFor)
        {
            if (std::u16string where = attraction_called(facts, p->state_target.id, strings); !where.empty())
                doing += u" " + where;
        }
        return doing;
    }

    InfoPage person_page(PersonTab tab, const PersonFacts& facts, const StringTable& strings, const InfoUnits& units)
    {
        InfoPage page;
        if (facts.person() == nullptr)
            return page;
        page.title = person_name(facts, strings);
        switch (tab)
        {
            case PersonTab::General:
                page.heading = person_doing(facts, strings);
                if (facts.guest != nullptr)
                    guest_general(page, *facts.guest, strings);
                else if (facts.staff != nullptr)
                    page.lines.push_back({ job(*facts.staff, strings), { } });
                break;
            case PersonTab::Thoughts:
                thoughts(page, facts, strings);
                break;
            case PersonTab::Rides:
                if (facts.guest != nullptr)
                    rides(page, *facts.guest, facts, strings);
                break;
            case PersonTab::Carrying:
                if (facts.guest != nullptr)
                    carrying(page, *facts.guest, facts, strings);
                break;
            case PersonTab::Stats:
                if (facts.guest != nullptr)
                    guest_stats(page, *facts.guest, strings);
                else if (facts.staff != nullptr)
                    staff_stats(page, *facts.staff, strings);
                break;
            case PersonTab::Cash:
                if (facts.guest != nullptr)
                    cash(page, *facts.guest, strings, units);
                break;
        }
        return page;
    }

} // namespace rct3::ui
