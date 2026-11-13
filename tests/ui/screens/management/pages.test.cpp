// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/management/pages.hpp"

#include "simulation/park/finance.hpp"
#include "ui/thought_text.hpp"

#include <algorithm>
#include <doctest/doctest.h>
#include <utility>

using namespace rct3;
using namespace rct3::ui;
namespace units = rct3::units;
using namespace rct3::ui::widgets;
using namespace rct3::ui::screens;
using namespace rct3::ui::screens::park_view;
using namespace rct3::ui::screens::management;
using namespace rct3::ui::screens::info;

namespace
{
    const InfoUnits kUnits;

    std::u16string money(std::int64_t amount)
    {
        return money_text(kUnits.currency, units::Money { amount });
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
        s.add("ShortMonth0", u"Mar");
        s.add("ShortMonth1", u"Apr");
        s.add("ShortMonth7", u"Oct");
        s.add("Rct3pp_Finance_Month", u"Month");
        s.add("Year", u"Year");
        s.add("Money_ShopSales", u"Shop sales");
        s.add("Money_ShopStock", u"Shop stock");
        s.add("ObjectiveCondition_ForManyMonths0", u"Held for");
        s.add("ObjectiveCondition_ForManyMonths1", u"months.");
        s.add("ObjectiveCondition_ParkGuests", u"Guests in park");
        s.add("ObjectiveCondition_VIP", u"VIP");
        s.add("ObjectiveCondition_AtAnyTime", u"At any time.");
        s.add("Rct3pp_GoalNotSimulated", u"Not simulated yet");
        s.add("Rct3pp_GoalNow", u"Now");
        s.add("Rct3pp_GoalMetNow", u"Met now");
        s.add("Rct3pp_GoalHeldSoFar", u"Held so far");
        s.add("ChallengeWindow_NoChallenge", u"None now.");
        return s;
    }

    save::model::Guest guest(std::string name, float happiness, bool in_park = true)
    {
        save::model::Guest g;
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
    // The staff window keeps one list of its own (staff_roster.hpp), without tabs.
    for (std::size_t t = 0; t < kManagementTopicCount; ++t)
        CHECK(management_tabs(static_cast<ManagementTopic>(t), test_strings(), kUnits).empty() == (static_cast<ManagementTopic>(t) == ManagementTopic::Staff));
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
    save::model::ParkModel park;
    park.has_finance = true;
    park.finance.cash = 50000;
    park.finance.loan = 20000;
    park.finance.ledger = { { .shop_sales = -1500 }, { .shop_sales = 700 } }; // Money in, then out
    simulation::park::ParkState state;
    state.cash = units::Money { 42000 };
    state.loan = units::Money { 20000 };
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

TEST_CASE("the finances window has our ledger, loan and attractions tabs, or the original's two when asked")
{
    const StringTable s = test_strings();
    CHECK(management_tabs(ManagementTopic::Finances, s, kUnits).size() == 5);
    CHECK(finance_page(0, false) == FinancePage::Summary);
    CHECK(finance_page(1, false) == FinancePage::Ledger);
    CHECK(finance_page(2, false) == FinancePage::Graphs);
    CHECK(finance_page(3, false) == FinancePage::Loan);
    CHECK(finance_page(4, false) == FinancePage::Attractions);
    InfoUnits original = kUnits;
    original.original_finances = true;
    CHECK(management_tabs(ManagementTopic::Finances, s, original).size() == 2);
    CHECK(finance_page(0, true) == FinancePage::Summary);
    CHECK(finance_page(1, true) == FinancePage::Graphs);
}

TEST_CASE("the finances ledger has a row for every month kept, named with its year, and the line picked")
{
    const StringTable s = test_strings();
    save::model::ParkModel park;
    park.has_finance = true;
    // Seven months back from March of the second year: more than a park file's five.
    park.finance.ledger.assign(7, { });
    park.finance.ledger[0] = { .shop_sales = -1500, .shop_stock = 400, .staff_wages = 300 };
    park.finance.ledger[1] = { .shop_sales = -200 };
    simulation::park::ParkState state;
    state.date = { .year = 1, .month = 0 };
    const ManagementPage page = management_page(ManagementTopic::Finances, 1, { .park = &park, .state = &state, .ledger_line = 9 }, s, kUnits);
    REQUIRE(page.rows.size() == 7);
    CHECK(page.sort == 0);
    CHECK(page.sort_descending);
    const TableRow& now = page.rows[0];
    CHECK(now.cells[0] == u"Mar, Year 2");
    CHECK(now.cells[1] == money(1500)); // In
    CHECK(now.cells[2] == money(700));  // Out: stock and wages
    CHECK(now.cells[3] == money(800));
    CHECK(page.rows[1].cells[0] == u"Oct, Year 1");
    // Newer months sort after older ones.
    CHECK(now.keys[0] > page.rows[1].keys[0]);
    REQUIRE(page.controls.size() == 1);
    CHECK(page.controls[0].id == kLedgerLineControl);
    CHECK(page.controls[0].value == 9);
    CHECK(page.controls[0].shown == u"Shop stock");
    CHECK(page.columns[4].heading == u"Shop stock");
    CHECK(now.cells[4] == money(-400));
}

TEST_CASE("the ledger writes out as comma-separated values, every line, the oldest month first")
{
    const StringTable s = test_strings();
    save::model::ParkModel park;
    park.has_finance = true;
    park.finance.ledger.resize(2);
    park.finance.ledger[0].ride_tickets = -1250;
    park.finance.ledger[1].staff_wages = 905;
    simulation::park::ParkState state;
    state.date = { .year = 0, .month = 1 };
    const std::string csv = ledger_csv({ .park = &park, .state = &state }, s);
    const auto first = csv.find('\n');
    REQUIRE(first != std::string::npos);
    const std::string heading = csv.substr(0, first);
    CHECK(heading.starts_with("\"Month\","));
    CHECK(std::ranges::count(heading, ',') == 22);
    const std::string older = csv.substr(first + 1, csv.find('\n', first + 1) - first - 1);
    const std::string newer = csv.substr(csv.find('\n', first + 1) + 1);
    CHECK(older.starts_with("\"Mar, Year 1\","));
    CHECK(older.find(",-9.05,") != std::string::npos);
    CHECK(newer.find(",12.50,") != std::string::npos);
    CHECK(ledger_csv({ }, s).empty());
}

TEST_CASE("the finances' attractions tab lists each ride and stall with its takings so far, the best first")
{
    const StringTable s = test_strings();
    save::model::RideModel rides;
    rides.rides.push_back({ });
    rides.rides[0].base = { .id = 7, .total_guests = 40, .total_profit = 9000, .running_cost = 120 };
    rides.stalls.push_back({ });
    rides.stalls[0].base = { .total_guests = 15, .total_profit = 500 };
    rides.stalls.push_back({ });
    rides.stalls[1].base.demolished = true;
    // Taken since the park was opened, on top of what the park file kept.
    const auto takings = [](std::uint64_t id) { return id == 7 ? AttractionTakings { .income = 1000, .customers = 5 } : AttractionTakings { }; };
    const ManagementPage page = management_page(ManagementTopic::Finances, 4, { .rides = &rides, .takings = takings }, s, kUnits);
    REQUIRE(page.rows.size() == 2);
    CHECK(page.sort == 3);
    CHECK(page.sort_descending);
    CHECK(page.rows[0].cells[1] == u"45");
    CHECK(page.rows[0].cells[3] == money(10000));
    CHECK(page.rows[0].cells[2] == money(120));
    CHECK(management_row(page.rows[1].id) == ManagementRow { ManagementRow::Kind::Stall, 0 });
}

TEST_CASE("the loan tab sets the loan with a bar and reckons the cash ahead from last month's profit")
{
    const StringTable s = test_strings();
    save::model::ParkModel park;
    park.has_finance = true;
    park.finance.max_loan = 1'000'000;
    park.finance.interest_rate_percent = 0;
    park.finance.ledger.resize(2);
    // Last month: 900.00 in, 200.00 out of which 50.00 was interest.
    park.finance.ledger[1].ride_tickets = -90'000;
    park.finance.ledger[1].staff_wages = 15'000;
    park.finance.ledger[1].loan_interest = 5'000;
    simulation::park::ParkState state;
    state.cash = units::Money { 100'000 };
    state.loan = units::Money { 300'000 };
    const ManagementPage page = management_page(ManagementTopic::Finances, 3, { .park = &park, .state = &state }, s, kUnits);
    REQUIRE(page.controls.size() == 1);
    CHECK(page.controls[0].id == kLoanControl);
    CHECK(page.controls[0].kind == InfoControlKind::Bar);
    CHECK(page.controls[0].value == 300'000);
    CHECK(page.controls[0].maximum == 1'000'000);
    CHECK(page.controls[0].step == simulation::park::kLoanStep.value);
    CHECK(page.controls[0].enabled);
    const auto value = [&](std::string_view key) {
        const auto found = std::ranges::find(page.lines, std::string(key), &InfoLine::name);
        REQUIRE(found != page.lines.end());
        return found->value;
    };
    // No interest at nought percent: 750.00 a month ahead, the 50.00 paid last month left out.
    CHECK(value("Rct3pp_Loan_In1") == money(100'000 + 75'000));
    CHECK(value("Rct3pp_Loan_In6") == money(100'000 + 6 * 75'000));
    // A month's interest on the loan now comes off each month.
    park.finance.interest_rate_percent = 10;
    const units::Money weekly = simulation::park::weekly_interest(units::Money { 300'000 }, 10);
    CHECK(weekly > units::Money { });
    const ManagementPage charged = management_page(ManagementTopic::Finances, 3, { .park = &park, .state = &state }, s, kUnits);
    const auto in2 = std::ranges::find(charged.lines, std::string("Rct3pp_Loan_In2"), &InfoLine::name);
    REQUIRE(in2 != charged.lines.end());
    CHECK(in2->value == money(100'000 + 2 * (75'000 - 4 * weekly.value)));
    // Without a full month behind it, nothing to go on.
    park.finance.ledger.resize(1);
    const ManagementPage early = management_page(ManagementTopic::Finances, 3, { .park = &park, .state = &state }, s, kUnits);
    const auto in1 = std::ranges::find(early.lines, std::string("Rct3pp_Loan_In1"), &InfoLine::name);
    REQUIRE(in1 != early.lines.end());
    CHECK(in1->value == u"-");
}

TEST_CASE("the loan buttons borrow and repay, each greyed out at its limit")
{
    const StringTable s = test_strings();
    save::model::ParkModel park;
    park.has_finance = true;
    park.finance.max_loan = 300'000;
    simulation::park::ParkState state { .cash = units::Money { 50'000 }, .loan = units::Money { 0 } };
    const auto enabled = [&] {
        const ManagementPage page = management_page(ManagementTopic::Finances, 0, { .park = &park, .state = &state }, s, kUnits);
        REQUIRE(page.buttons.size() == 2);
        CHECK(page.buttons[kBorrowButton].key == "Rct3pp_Borrow");
        CHECK(page.buttons[kRepayButton].key == "Rct3pp_Repay");
        return std::pair { page.buttons[kBorrowButton].enabled, page.buttons[kRepayButton].enabled };
    };
    CHECK(enabled() == std::pair { true, false }); // No loan to repay
    state.loan = units::Money { 300'000 };
    state.cash = units::Money { 350'000 };
    CHECK(enabled() == std::pair { false, true }); // At the maximum
    state.cash = units::Money { 10'000 };
    CHECK(enabled() == std::pair { false, false }); // Not the cash to repay a step
    state.cash.reset();
    state.loan = units::Money { 0 };
    CHECK(enabled() == std::pair { false, false }); // Money never runs out
    CHECK(std::ranges::none_of(management_page(ManagementTopic::Finances, 0, { }, s, kUnits).buttons, &ManagementButton::enabled));
}

TEST_CASE("a weekly graph leaves out the week still running, oldest first, with a mark where each month began")
{
    const StringTable s = test_strings();
    save::model::ParkModel park;
    park.has_park = true;
    park.park.guests_in_park = { .samples = { 50, 40, 20 }, .capacity = 64 };
    park.park.adults_in_park = { .samples = { 30, 10, 5 }, .capacity = 64 };
    simulation::park::ParkState state;
    state.time = 5 * save::model::GameClock::kUnitsPerWeek; // The second week of the second month
    const ManagementPage page = management_page(ManagementTopic::ParkOperations, 1, { .park = &park, .state = &state }, s, kUnits);
    REQUIRE(page.graph.size() == 2);
    const auto guests = std::ranges::find(page.graph, std::string("Graph_Guests"), &InfoSeries::key);
    REQUIRE(guests != page.graph.end());
    CHECK(guests->values == std::vector<float> { 20, 40 });
    // The guests by age read on the scale of all of them.
    CHECK(guests->full == doctest::Approx(50));
    CHECK(page.graph[0].key == "Graph_AdultsInPark");
    CHECK(page.graph[0].full == doctest::Approx(50));
    CHECK(page.graph[0].axis.front() == u"50");
    CHECK(page.graph[0].axis.back() == u"0");
    CHECK(page.graph_slots == 63);
    // The week gone began the month.
    REQUIRE_FALSE(page.graph_ticks.empty());
    CHECK(page.graph_ticks[0].back == 0);
    CHECK(page.graph_ticks[0].label == u"Apr");
}

TEST_CASE("a weekly graph of money that went below nought has nought across its middle")
{
    const StringTable s = test_strings();
    save::model::ParkModel park;
    park.has_finance = true;
    park.finance.weekly_profit = { .samples = { 0, -300, 100 }, .capacity = 64 };
    const ManagementPage page = management_page(ManagementTopic::Finances, 2, { .park = &park }, s, kUnits);
    REQUIRE(page.graph.size() == 1);
    CHECK(page.graph[0].centred);
    CHECK(page.graph[0].full == doctest::Approx(500));
    REQUIRE(page.graph[0].axis.size() == 5);
    CHECK(page.graph[0].axis[2] == money(0));
    CHECK(page.graph[0].axis[4] == money(-500));
    // Money too small to see reads against a whole unit.
    park.finance.weekly_profit = { .samples = { 0, 0, 0 }, .capacity = 64 };
    CHECK(management_page(ManagementTopic::Finances, 2, { .park = &park }, s, kUnits).graph[0].full == doctest::Approx(100));
}

TEST_CASE("a weekly graph has no month marks from before the calendar began")
{
    const StringTable s = test_strings();
    save::model::ParkModel park;
    park.park.guests_in_park = { .samples = { 5, 5, 5 }, .capacity = 64 };
    simulation::park::ParkState state;
    state.time = 2 * save::model::GameClock::kUnitsPerWeek;
    const ManagementPage page = management_page(ManagementTopic::ParkOperations, 1, { .park = &park, .state = &state }, s, kUnits);
    REQUIRE(page.graph_ticks.size() == 1);
    CHECK(page.graph_ticks[0].back == 1);
}

TEST_CASE("the guest statistics average over the guests in the park")
{
    const StringTable s = test_strings();
    save::model::PeopleModel people;
    people.guests = { guest("Ann", 0.2f), guest("Bob", 0.6f), guest("Cy", 1.0f, false) };
    const ManagementPage page = management_page(ManagementTopic::Guests, 2, { .people = &people }, s, kUnits);
    CHECK(value_of(page, u"Number of guests:") == u"2");
    CHECK(value_of(page, u"Average happiness:") == u"40 %");
}

TEST_CASE("each guest's row has the face of how happy they are")
{
    const StringTable s = test_strings();
    save::model::PeopleModel people;
    people.guests = { guest("Ann", 0.1f), guest("Bob", 0.5f), guest("Cy", 0.9f) };
    const ManagementPage page = management_page(ManagementTopic::Guests, 0, { .people = &people }, s, kUnits);
    REQUIRE(page.rows.size() == 3);
    CHECK(page.rows[0].icon == mood_icon(Mood::Sad));
    CHECK(page.rows[1].icon == mood_icon(Mood::Neutral));
    CHECK(page.rows[2].icon == mood_icon(Mood::Happy));
}

TEST_CASE("the families tab lists the player's groups with their leader and members")
{
    const StringTable s = test_strings();
    save::model::PeopleModel people;
    people.guests = { guest("Ann", 0.5f), guest("Bob", 0.5f), guest("Cy", 0.5f) };
    for (std::size_t i = 0; i < people.guests.size(); ++i)
        people.guests[i].person.id = 100 + i;
    save::model::GuestGroup family;
    family.group_type = 10;
    family.leader = { 101 };
    family.members = { { 100 }, { 101 }, { } };
    save::model::GuestGroup outing; // Not the player's
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
    save::model::RideModel rides;
    rides.rides.resize(3);
    rides.rides[2].kind = save::model::RideKind::Special;
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
    save::model::ParkModel park;
    save::model::Condition c;
    c.kind = save::model::ConditionKind::ParkGuests;
    c.difficulty = 1;
    c.months_to_hold = 5;
    c.value = 800;
    park.objectives.conditions = { c };
    const ManagementPage page = management_page(ManagementTopic::Objectives, 1, { .park = &park }, s, kUnits);
    CHECK(value_of(page, u"Guests in park") == u"800");
    CHECK(value_of(page, u"Held for 5 months.") == u"-");
    CHECK(management_page(ManagementTopic::Objectives, 0, { .park = &park }, s, kUnits).lines.size() == 2);
}

TEST_CASE("an objective's condition shows where the park stands on it and how long it has held")
{
    const StringTable s = test_strings();
    save::model::ParkModel park;
    save::model::Condition c;
    c.kind = save::model::ConditionKind::ParkGuests;
    c.months_to_hold = 5;
    c.value = 800;
    c.start_time = 0;
    c.consistency_flags = std::to_underlying(save::model::ConditionFlag::TimerRunning);
    park.objectives.conditions = { c };
    simulation::park::ParkState state;
    state.time = 1.5f * save::model::GameClock::kUnitsPerMonth;
    const std::array goals { simulation::GoalReading { .now = 900, .met = true } };
    const ManagementPage page = management_page(ManagementTopic::Objectives, 0, { .park = &park, .state = &state, .goals = goals }, s, kUnits);
    CHECK(value_of(page, u"Held for 5 months.") == u"Met now");
    CHECK(value_of(page, u"Now") == u"900");
    CHECK(value_of(page, u"Held so far") == u"1.5");
}

TEST_CASE("an objective's condition the simulation cannot judge says so")
{
    const StringTable s = test_strings();
    save::model::ParkModel park;
    park.objectives.conditions = { { .kind = save::model::ConditionKind::PoolGuests } };
    const ManagementPage page = management_page(ManagementTopic::Objectives, 0, { .park = &park }, s, kUnits);
    CHECK(value_of(page, u"At any time.") == u"Not simulated yet");
}

TEST_CASE("the research window sets the budget and a priority an area")
{
    StringTable s = test_strings();
    s.add("Rct3pp_Research_Current", u"Researching:");
    s.add("Rct3pp_Research_MonthsLeft", u"About # months left");
    s.add("Rct3pp_Research_NothingLeft", u"Nothing left");
    s.add("Carousel_Name", u"Carousel");
    simulation::Research research { .budget = 5000, .spent = 4000, .current = 7, .priorities = { 0.5f, 0.25f, 1.0f } };
    std::vector<simulation::ResearchItem> left { { .id = 7, .area = simulation::ResearchArea::GentleRides, .cost = units::Money { 16000 } } };
    ManagementPage page = management_page(ManagementTopic::Research, 0, { .research = &research, .research_left = left, .research_name = [](std::uint64_t) { return std::string_view("Carousel_Name"); } }, s, kUnits);
    CHECK(value_of(page, u"Researching:") == u"Carousel");
    REQUIRE(page.lines.size() == 2);
    CHECK(page.lines[1].label == u"About 3 months left"); // 120.00 left at 50.00 a month
    CHECK(page.lines[1].value == u"25 %");
    CHECK(page.lines[1].bar == doctest::Approx(0.25));

    REQUIRE(page.controls.size() == 2 + 4); // The budget, the areas' heading, three areas kept and the one with an item
    CHECK(page.controls[0].id == kResearchBudgetControl);
    CHECK(page.controls[0].value == 5000);
    CHECK(page.controls[0].shown == money(5000));
    CHECK(page.controls[3].id == kFirstPriorityControl + 1);
    CHECK(page.controls[3].value == 25);
    CHECK(page.controls[3].shown == u"25 %");
    CHECK(page.controls[5].id == kFirstPriorityControl + static_cast<int>(simulation::ResearchArea::GentleRides));
    CHECK(page.controls[5].value == 100); // Not set, so the most

    left.clear();
    page = management_page(ManagementTopic::Research, 0, { .research = &research, .research_left = left }, s, kUnits);
    CHECK(value_of(page, u"Researching:") == u"Nothing left");
    CHECK(page.lines.size() == 1);
}

TEST_CASE("the research window lists what was invented, newest first, with its date")
{
    StringTable s = test_strings();
    s.add("Carousel_Name", u"Carousel");
    save::model::ParkModel park;
    park.content.entries = { { .id = 7 }, { .id = 8 } };
    const simulation::Research research { .invented = { 7, 8 }, .invented_at = { 0, 8 * save::model::GameClock::kUnitsPerMonth } }; // A year apart
    const ManagementPage page = management_page(ManagementTopic::Research, kInventedTab, { .park = &park, .research = &research, .research_name = [](std::uint64_t id) { return std::string_view(id == 7 ? "Carousel_Name" : ""); } }, s, kUnits);
    REQUIRE(page.rows.size() == 2);
    CHECK(page.sort == 1);
    CHECK(page.sort_descending);
    CHECK(page.rows[0].cells[0] == u"Carousel");
    CHECK(page.rows[1].cells[0] == u"-"); // Not named
    CHECK(page.rows[0].cells[1] != page.rows[1].cells[1]);
    CHECK(management_row(page.rows[1].id).index == 1);
}

TEST_CASE("the research window's list of what may come next offers a focus on the row picked")
{
    StringTable s = test_strings();
    s.add("Rct3pp_Research_Focus", u"Focus (%AMOUNT%)");
    const simulation::Research research { .budget = 20000, .current = 1 };
    const std::vector<simulation::ResearchItem> left {
        { .id = 1, .area = simulation::ResearchArea::Shops, .cost = units::Money { 10000 } },
        { .id = 2, .area = simulation::ResearchArea::Shops, .cost = units::Money { 6000 } },
        { .id = 3, .area = simulation::ResearchArea::Coasters, .cost = units::Money { 24000 } },
    };
    ManagementPage page = management_page(ManagementTopic::Research, kComingTab, { .research = &research, .research_left = left }, s, kUnits);
    CHECK(page.rows.size() == 2); // Not the item under way
    REQUIRE(page.buttons.size() == 2);
    CHECK_FALSE(page.buttons[kFocusButton].enabled);
    CHECK_FALSE(page.buttons[kUnfocusButton].enabled);
    page = management_page(ManagementTopic::Research, kComingTab, { .research = &research, .research_left = left, .research_pick = 2 }, s, kUnits);
    CHECK(page.buttons[kFocusButton].enabled);
    CHECK(page.buttons[kFocusButton].text == u"Focus (" + money(9000) + u")"); // 60.00 half as much again
}

TEST_CASE("without challenges the window says there are none")
{
    const StringTable s = test_strings();
    const save::model::ParkModel park;
    const ManagementPage page = management_page(ManagementTopic::Challenges, 0, { .park = &park }, s, kUnits);
    CHECK(page.note == u"None now.");
    CHECK(page.rows.empty());
}

TEST_CASE("a park's name fills in the scenario's for its placeholder")
{
    StringTable strings;
    strings.add("SandboxScenario_Name", u"Sandbox");
    strings.add("Valley_Name", u"Valley");
    save::model::Park park;
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

TEST_CASE("the finances show what the park and the company are worth now")
{
    StringTable s = test_strings();
    s.add("FinancialSummary_ParkValue", u"Park value:");
    s.add("FinancialSummary_CompanyValue", u"Company value:");
    save::model::ParkModel park;
    park.has_finance = true;
    park.finance.park_value = { .samples = { 1 } };
    save::model::RideModel rides;
    simulation::park::ParkState state;
    state.cash = units::Money { 42000 };
    state.loan = units::Money { 20000 };
    state.guests = 3;
    const units::Money worth = simulation::park::park_value(3, rides);
    const ManagementPage page = management_page(ManagementTopic::Finances, 0, { .park = &park, .state = &state, .rides = &rides }, s, kUnits);
    CHECK(value_of(page, u"Park value:") == money(worth.value));
    CHECK(value_of(page, u"Company value:") == money(42000 - 20000 + worth.value));
}
