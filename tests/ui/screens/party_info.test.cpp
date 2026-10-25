// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/party_info.hpp"

#include <algorithm>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;

namespace
{
    StringTable table()
    {
        StringTable s;
        s.add("GuestGroupType_Family", u"Family");
        s.add("GuestDetails_GroupHelp", u"Party");
        s.add("Guest_Happiness", u"Glad:");
        s.add("Guest_Hunger", u"Peckish:");
        s.add("Guest_CashInPocket", u"Purse:");
        s.add("Guest_FavouriteRide", u"Best:");
        s.add("PersonState_Queueing", u"Waiting");
        return s;
    }

    struct Park
    {
        save::PeopleModel people;
        save::RideModel rides;
    };

    Park sample_park()
    {
        Park p;
        for (const std::string_view name : { "Twister", "Zoom", "Drop" })
        {
            save::Ride& r = p.rides.rides.emplace_back();
            r.base.id = p.rides.rides.size();
            r.base.name = std::string(name);
        }
        save::Guest& ada = p.people.guests.emplace_back();
        ada.person.name = "Ada";
        ada.person.state = save::PersonState::Queueing;
        ada.mood_now.happiness = 0.8f;
        ada.needs.food = 0.9f;
        ada.cash.current = 500;
        ada.cash.total_spent = 1200;
        ada.cash.spent_rides = 700;
        ada.cash.spent_park_entry = 500;
        ada.favourite_ride = { 2 };
        ada.person.attractions_visited = { { 1 }, { 2 } };
        save::Guest& bo = p.people.guests.emplace_back();
        bo.person.name = "Bo";
        bo.mood_now.happiness = 0.4f;
        bo.needs.food = 0.3f;
        bo.cash.current = 250;
        bo.cash.total_spent = 300;
        bo.cash.spent_food = 300;
        bo.cash.withdrawn = 1000;
        bo.person.attractions_visited = { { 2 }, { 3 } };
        return p;
    }

    PartyFacts party(const Park& p, std::optional<std::int32_t> kind = std::nullopt)
    {
        PartyFacts f { .kind = kind };
        for (const save::Guest& g : p.people.guests)
            f.members.push_back({ .guest = &g, .people = &p.people, .rides = &p.rides });
        return f;
    }

    std::u16string value(const InfoPage& page, std::string_view name)
    {
        const auto line = std::ranges::find(page.lines, name, &InfoLine::name);
        return line != page.lines.end() ? line->value : u"(none)";
    }
} // namespace

TEST_CASE("party_page: the kind names the party, the leader heads it")
{
    const Park p = sample_park();
    CHECK(party_page(PartyTab::Stats, party(p, 6), table(), { }).title == u"Family");
    const InfoPage unknown = party_page(PartyTab::Stats, party(p), table(), { });
    CHECK(unknown.title == u"Party");
    CHECK(unknown.heading == u"Ada");
}

TEST_CASE("party_page: stats average the moods and take the most pressing need")
{
    const InfoPage page = party_page(PartyTab::Stats, party(sample_park()), table(), { });
    CHECK(value(page, "Guest_Happiness") == u"60 %");
    CHECK(value(page, "Guest_Hunger") == u"70 %");
}

TEST_CASE("party_page: cash adds up the members' money, row by row as one guest's")
{
    const Park p = sample_park();
    const InfoUnits units { .currency = { .before = { }, .after = { }, .decimal = u"." } };
    const InfoPage page = party_page(PartyTab::Cash, party(p), table(), units);
    CHECK(value(page, "Guest_CashInPocket") == u"7.50");
    CHECK(value(page, "Guest_CashSpent") == u"15.00");
    CHECK(value(page, "Guest_SpentOnRides") == u"7.00");
    CHECK(value(page, "Guest_SpentOnFood") == u"3.00");
    CHECK(value(page, "Guest_SpentOnDrink") == u"0.00");
    CHECK(value(page, "Guest_PaidToEnterPark") == u"5.00");
    CHECK(value(page, "Guest_CashWithdrawn") == u"10.00");
    // The same rows, in the same order and halves, as the guest window's Cash tab.
    const InfoPage one = person_page(PersonTab::Cash, party(p).members.front(), table(), units);
    REQUIRE(one.lines.size() == page.lines.size());
    for (std::size_t i = 0; i < one.lines.size(); ++i)
    {
        CHECK(one.lines[i].name == page.lines[i].name);
        CHECK(one.lines[i].half == page.lines[i].half);
    }
    CHECK(value(one, "Guest_CashInPocket") == u"5.00");
    CHECK(page.heading.empty());
}

TEST_CASE("party_page: rides list each ride any member went on once, after the leader's favourite")
{
    const InfoPage page = party_page(PartyTab::Rides, party(sample_park()), table(), { });
    CHECK(value(page, "Guest_FavouriteRide") == u"Zoom");
    std::vector<std::u16string> names;
    for (const InfoLine& line : page.lines)
        if (line.value.empty())
            names.push_back(line.label);
    CHECK(names == std::vector<std::u16string> { u"Zoom", u"Twister", u"Drop" });
}

TEST_CASE("party_page: members are buttons by their place, the leader's coloured")
{
    const InfoPage page = party_page(PartyTab::Members, party(sample_park()), table(), { });
    REQUIRE(page.controls.size() == 2);
    CHECK(page.controls[0].text == u"Ada");
    CHECK(page.controls[0].label == u"Waiting");
    CHECK(page.controls[0].colour.has_value());
    CHECK(page.controls[1].id == 1);
    CHECK(page.controls[1].text == u"Bo");
    CHECK_FALSE(page.controls[1].colour.has_value());
}
