// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/party_info.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <ranges>
#include <string>
#include <utility>

namespace rct3::ui
{

    namespace
    {
        /// The names of the kinds of party, in the save's numbering.
        constexpr std::array<std::string_view, 11> kKinds = {
            "GuestGroupType_LoneTeenager", "GuestGroupType_LoneAdult", "GuestGroupType_TeenageCouple", "GuestGroupType_AdultCouple", "GuestGroupType_TeenageGang", "GuestGroupType_AdultParty", "GuestGroupType_Family", "GuestGroupType_DayTrip", "GuestGroupType_LoneVIP", "GuestGroupType_VIPEntourage", "GuestGroupType_AvatarFamily",
        };
        /// Words for a guest's liking of intense rides, gentlest first.
        constexpr std::array<std::string_view, 4> kIntensityWords = { "PersonEditor_Gentle", "PersonEditor_Normal", "PersonEditor_Wild", "PersonEditor_Crazy" };
        constexpr std::size_t kListLines = 6; ///< As many as the page has room for
        constexpr float kPercent = 100;
        constexpr Color kLeaderColour = hex("#ffd75e"); ///< Of the leader's row

        std::u16string text(const StringTable& strings, std::string_view key)
        {
            return std::u16string(strings.get(key));
        }

        void add(InfoPage& page, const StringTable& strings, std::string_view label, std::u16string value, bool half = false)
        {
            page.lines.push_back({ text(strings, label), std::move(value), half, std::string(label) });
        }

        std::u16string percent(float fraction)
        {
            const std::string s = std::format("{} %", std::lround(std::clamp(fraction, 0.0f, 1.0f) * kPercent));
            return { s.begin(), s.end() };
        }

        /// The line named `name` of the leader's page of `tab`, when it has one.
        std::optional<InfoLine> leader_line(const PartyFacts& f, PersonTab tab, std::string_view name, const StringTable& strings, const InfoUnits& units)
        {
            const InfoPage page = person_page(tab, f.members.front(), strings, units);
            const auto line = std::ranges::find(page.lines, name, &InfoLine::name);
            return line != page.lines.end() ? std::optional(*line) : std::nullopt;
        }

        std::u16string ride_called(const PersonFacts& f, save::PeopleObjectId id, const StringTable& strings)
        {
            if (f.rides == nullptr)
                return { };
            if (const auto r = std::ranges::find(f.rides->rides, id, [](const save::Ride& r) { return r.base.id; }); r != f.rides->rides.end())
                return attraction_name(r->base, strings);
            if (const auto s = std::ranges::find(f.rides->stalls, id, [](const save::Stall& s) { return s.base.id; }); s != f.rides->stalls.end())
                return attraction_name(s->base, strings);
            return { };
        }

        // The leader's favourite and count, as theirs; then the rides any of them went on, each once, each
        // member's latest first.
        void rides(InfoPage& page, const PartyFacts& f, const StringTable& strings, const InfoUnits& units)
        {
            for (const std::string_view name : { "Guest_FavouriteRide", "Guest_RidesBeenOn" })
                if (auto line = leader_line(f, PersonTab::Rides, name, strings, units))
                    page.lines.push_back(std::move(*line));
            std::vector<save::PeopleObjectId> listed;
            for (const PersonFacts& m : f.members)
            {
                for (auto it = m.guest->person.attractions_visited.rbegin(); it != m.guest->person.attractions_visited.rend() && page.lines.size() < kListLines; ++it)
                {
                    if (std::ranges::contains(listed, it->id))
                        continue;
                    listed.push_back(it->id);
                    if (std::u16string name = ride_called(m, it->id, strings); !name.empty())
                        page.lines.push_back({ std::move(name), { } });
                }
            }
        }

        // How they feel on average, and the most pressing of their needs, which the party goes by.
        void stats(InfoPage& page, const PartyFacts& f, const StringTable& strings, const InfoUnits& units)
        {
            const auto mean = [&](auto&& of) {
                float sum = 0;
                for (const PersonFacts& m : f.members)
                    sum += of(*m.guest);
                return sum / static_cast<float>(f.members.size());
            };
            const auto most = [&](auto&& of) {
                float top = 0;
                for (const PersonFacts& m : f.members)
                    top = std::max(top, of(*m.guest));
                return top;
            };
            add(page, strings, "Guest_Happiness", percent(mean([](const save::Guest& g) { return g.mood_now.happiness; })), true);
            add(page, strings, "Guest_Energy", percent(mean([](const save::Guest& g) { return g.mood_now.energy; })), true);
            add(page, strings, "Guest_Hunger", percent(most([](const save::Guest& g) { return 1 - g.needs.food; })), true);
            add(page, strings, "Guest_Thirst", percent(most([](const save::Guest& g) { return 1 - g.needs.drink; })), true);
            add(page, strings, "Guest_Toilet", percent(most([](const save::Guest& g) { return g.needs.toilet; })), true);
            add(page, strings, "Guest_Nausea", percent(mean([](const save::Guest& g) { return g.mood_now.nausea; })), true);
            if (std::ranges::all_of(f.members, [](const PersonFacts& m) { return m.guest->mood_now.sunburn.has_value(); }))
                add(page, strings, "Guest_SunBurn", percent(mean([](const save::Guest& g) { return *g.mood_now.sunburn; })), true);
            // From the gentlest liking among them to the wildest.
            const auto [low, high] = std::ranges::minmax(f.members | std::views::transform([](const PersonFacts& m) { return static_cast<std::size_t>(m.guest->ride_preference.intensity); }));
            if (high < kIntensityWords.size())
                add(page, strings, "Guest_PreferredRideIntensity", low == high ? text(strings, kIntensityWords[low]) : text(strings, kIntensityWords[low]) + u" - " + text(strings, kIntensityWords[high]));
            if (auto line = leader_line(f, PersonTab::General, "Guest_TimeInPark", strings, units))
                page.lines.push_back(std::move(*line));
        }

        // A row for each, what they are doing beside their name; the leader's in its own colour.
        void members(InfoPage& page, const PartyFacts& f, const StringTable& strings)
        {
            for (std::size_t i = 0; i < f.members.size(); ++i)
            {
                InfoControl& row = page.controls.emplace_back();
                row.id = static_cast<int>(i);
                row.name = std::format("member{}", i);
                row.kind = InfoControlKind::Button;
                row.label = person_doing(f.members[i], strings);
                row.text = person_name(f.members[i], strings);
                if (i == 0)
                {
                    row.colour = kLeaderColour;
                    row.help = text(strings, "Rct3pp_PartyLeader");
                }
            }
        }

    } // namespace

    InfoPage party_page(PartyTab tab, const PartyFacts& facts, const StringTable& strings, const InfoUnits& units)
    {
        InfoPage page;
        if (facts.members.empty() || !std::ranges::all_of(facts.members, [](const PersonFacts& m) { return m.guest != nullptr; }))
            return page;
        const auto kind = static_cast<std::size_t>(facts.kind.value_or(-1));
        page.title = kind < kKinds.size() ? text(strings, kKinds[kind]) : text(strings, "GuestDetails_GroupHelp");
        // Not over the cash rows, which fill the page as in the guest window.
        if (tab != PartyTab::Cash)
            page.heading = person_name(facts.members.front(), strings);
        switch (tab)
        {
            case PartyTab::Rides:
                rides(page, facts, strings, units);
                break;
            case PartyTab::Stats:
                stats(page, facts, strings, units);
                break;
            case PartyTab::Cash:
                cash_lines(page, facts.members, strings, units); // The guest window's rows, each summed over them
                break;
            case PartyTab::Members:
                members(page, facts, strings);
                break;
        }
        return page;
    }

} // namespace rct3::ui
