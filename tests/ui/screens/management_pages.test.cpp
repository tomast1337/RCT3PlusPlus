// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/management_pages.hpp"

#include <algorithm>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;

namespace
{
    const InfoUnits kUnits;

    std::u16string money(std::int64_t amount)
    {
        return money_text(kUnits.currency, amount);
    }

    /// The value of the line labelled `label`, or nothing.
    std::optional<std::u16string> value_of(const ManagementPage& page, std::u16string_view label)
    {
        const auto it = std::ranges::find(page.lines, label, &InfoLine::label);
        return it != page.lines.end() ? std::optional(it->value) : std::nullopt;
    }

    StringTable test_strings()
    {
        StringTable s;
        s.add("Park_Name", u"Park name:");
        s.add("FinancialSummary_Cash", u"Cash:");
        s.add("FinancialSummary_Loan", u"Loan:");
        s.add("GuestStats_AverageHappiness", u"Average happiness:");
        s.add("GuestStats_NumGuests", u"Number of guests:");
        s.add("Month0", u"Spring");
        s.add("Month7", u"Autumn");
        s.add("Money_ShopSales", u"Shop sales");
        s.add("ObjectiveCondition_ForManyMonths0", u"Held for");
        s.add("ObjectiveCondition_ForManyMonths1", u"months.");
        s.add("ObjectiveCondition_ParkGuests", u"Guests in park");
        s.add("ChallengeWindow_NoChallenge", u"None now.");
        return s;
    }

    save::Guest guest(std::string name, float happiness, bool in_park = true)
    {
        save::Guest g;
        g.person.name = std::move(name);
        g.mood_now.happiness = happiness;
        g.in_park = in_park;
        return g;
    }
} // namespace

TEST_CASE("each management menu item opens its own window, and only those")
{
    CHECK(management_topic(ToolItem::Finances) == ManagementTopic::Finances);
    CHECK(management_topic(ToolItem::ParkOperations) == ManagementTopic::ParkOperations);
    CHECK(management_topic(ToolItem::Challenges) == ManagementTopic::Challenges);
    CHECK_FALSE(management_topic(ToolItem::People));
    for (std::size_t t = 0; t < kManagementTopicCount; ++t)
        CHECK_FALSE(management_tabs(static_cast<ManagementTopic>(t), test_strings()).empty());
}

TEST_CASE("a row's id gives back what it is about")
{
    const ManagementRow row { ManagementRow::Kind::Stall, 12345 };
    CHECK(management_row(management_row_id(row)) == row);
    CHECK(management_row(management_row_id({ ManagementRow::Kind::Guest, 0 })).kind == ManagementRow::Kind::Guest);
}

TEST_CASE("without the park every value is a dash")
{
    const StringTable s = test_strings();
    const ManagementPage page = management_page(ManagementTopic::ParkOperations, 0, { }, s, kUnits);
    CHECK(value_of(page, u"Park name:") == u"-");
    const ManagementPage cash = management_page(ManagementTopic::Finances, 0, { }, s, kUnits);
    CHECK(value_of(cash, u"Cash:") == u"-");
    CHECK(cash.columns.empty());
}

TEST_CASE("the finances show the running cash and the ledger a month a column, this month first, money in above nought")
{
    const StringTable s = test_strings();
    save::ParkModel park;
    park.has_finance = true;
    park.finance.cash = 50000;
    park.finance.loan = 20000;
    park.finance.ledger_periods_loaded = 2;
    park.finance.ledger[0].shop_sales = -1500; // Money in
    park.finance.ledger[1].shop_sales = 700;
    simulation::ParkState state;
    state.cash = 42000;
    state.loan = 20000;
    state.date.month = 0;
    const ManagementPage page = management_page(ManagementTopic::Finances, 0, { .park = &park, .state = &state }, s, kUnits);
    CHECK(value_of(page, u"Cash:") == money(42000));
    CHECK(value_of(page, u"Loan:") == money(20000));
    REQUIRE(page.columns.size() == 3);
    CHECK(page.columns[1].heading == u"Spring");
    CHECK(page.columns[2].heading == u"Autumn");
    const auto sales = std::ranges::find_if(page.rows, [](const TableRow& r) { return r.cells.front() == u"Shop sales"; });
    REQUIRE(sales != page.rows.end());
    CHECK(sales->cells[1] == money(1500));
    CHECK(sales->cells[2] == money(-700));
}

TEST_CASE("the staff tabs list one job each, the first everyone")
{
    const StringTable s = test_strings();
    save::PeopleModel people;
    for (const save::StaffKind kind : { save::StaffKind::Handyman, save::StaffKind::Mechanic, save::StaffKind::Handyman })
    {
        save::Staff member;
        member.kind = kind;
        people.staff.push_back(member);
    }
    const ManagementFacts facts { .people = &people };
    CHECK(management_page(ManagementTopic::Staff, 0, facts, s, kUnits).rows.size() == 3);
    const ManagementPage handymen = management_page(ManagementTopic::Staff, 1, facts, s, kUnits);
    REQUIRE(handymen.rows.size() == 2);
    CHECK(management_row(handymen.rows[1].id) == ManagementRow { ManagementRow::Kind::Staff, 2 });
    CHECK(management_page(ManagementTopic::Staff, 2, facts, s, kUnits).rows.size() == 1);
}

TEST_CASE("the guest statistics average over the guests in the park")
{
    const StringTable s = test_strings();
    save::PeopleModel people;
    people.guests = { guest("Ann", 0.2f), guest("Bob", 0.6f), guest("Cy", 1.0f, false) };
    const ManagementPage page = management_page(ManagementTopic::Guests, 2, { .people = &people }, s, kUnits);
    CHECK(value_of(page, u"Number of guests:") == u"2");
    CHECK(value_of(page, u"Average happiness:") == u"40 %");
}

TEST_CASE("the families tab lists the player's groups with their leader and members")
{
    const StringTable s = test_strings();
    save::PeopleModel people;
    people.guests = { guest("Ann", 0.5f), guest("Bob", 0.5f), guest("Cy", 0.5f) };
    for (std::size_t i = 0; i < people.guests.size(); ++i)
        people.guests[i].person.id = 100 + i;
    save::GuestGroup family;
    family.group_type = 10;
    family.leader = { 101 };
    family.members = { { 100 }, { 101 }, { } };
    save::GuestGroup outing; // Not the player's
    outing.group_type = 6;
    outing.members = { { 102 } };
    people.groups = { family, outing };
    const ManagementPage page = management_page(ManagementTopic::Guests, 3, { .people = &people }, s, kUnits);
    REQUIRE(page.rows.size() == 1);
    CHECK(page.rows[0].cells[0] == u"Bob");
    CHECK(page.rows[0].cells[1] == u"Ann, Bob");
    CHECK(page.rows[0].cells[2] == u"2");
    CHECK(management_row(page.rows[0].id) == ManagementRow { ManagementRow::Kind::Guest, 1 });
    CHECK(std::ranges::none_of(page.buttons, &ManagementButton::enabled)); // Families are made elsewhere
}

TEST_CASE("the attraction tabs split rides, stalls and facilities")
{
    const StringTable s = test_strings();
    save::RideModel rides;
    rides.rides.resize(3);
    rides.rides[2].kind = save::RideKind::Special;
    rides.stalls.resize(2);
    const ManagementFacts facts { .rides = &rides };
    CHECK(management_page(ManagementTopic::Attractions, 0, facts, s, kUnits).rows.size() == 2);
    CHECK(management_page(ManagementTopic::Attractions, 1, facts, s, kUnits).rows.size() == 2);
    CHECK(management_page(ManagementTopic::Attractions, 2, facts, s, kUnits).rows.size() == 1);
    CHECK(management_page(ManagementTopic::Attractions, 3, facts, s, kUnits).rows.size() == 5);
}

TEST_CASE("an objective's condition says how long it must hold")
{
    const StringTable s = test_strings();
    save::ParkModel park;
    save::Condition c;
    c.kind = save::ConditionKind::ParkGuests;
    c.difficulty = 1;
    c.months_to_hold = 5;
    c.value = 800;
    park.objectives.conditions = { c };
    const ManagementPage page = management_page(ManagementTopic::Objectives, 1, { .park = &park }, s, kUnits);
    CHECK(value_of(page, u"Guests in park") == u"800");
    CHECK(value_of(page, u"Held for 5 months.") == u"-");
    CHECK(management_page(ManagementTopic::Objectives, 0, { .park = &park }, s, kUnits).lines.size() == 2);
}

TEST_CASE("the research priorities show one row an area")
{
    const StringTable s = test_strings();
    save::ParkModel park;
    park.research.has_simple_research = true;
    park.research.priorities = { 0.5f, 0.25f, 1.0f };
    const ManagementPage page = management_page(ManagementTopic::Research, 0, { .park = &park }, s, kUnits);
    REQUIRE(page.rows.size() == 3);
    CHECK(page.rows[1].cells[1] == u"25 %");
}

TEST_CASE("without challenges the window says there are none")
{
    const StringTable s = test_strings();
    const save::ParkModel park;
    const ManagementPage page = management_page(ManagementTopic::Challenges, 0, { .park = &park }, s, kUnits);
    CHECK(page.note == u"None now.");
    CHECK(page.rows.empty());
}

TEST_CASE("a park's name fills in the scenario's for its placeholder")
{
    StringTable strings;
    strings.add("SandboxScenario_Name", u"Sandbox");
    strings.add("Valley_Name", u"Valley");
    save::Park park;
    park.name = "#ME#";
    park.scenario_name = "#SANDBOX#";
    CHECK(park_name(park, strings) == u"Sandbox");
    park.name = "#ME# Two";
    park.scenario_name = "Valley_Name";
    CHECK(park_name(park, strings) == u"Valley Two");
    park.name.clear();
    CHECK(park_name(park, strings) == u"Valley");
    park.name = "Valley_Name";
    CHECK(park_name(park, strings) == u"Valley");
    park.name = "My Park";
    CHECK(park_name(park, strings) == u"My Park");
}
