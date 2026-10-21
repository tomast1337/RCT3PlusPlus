// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/attraction_info.hpp"

#include <algorithm>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;

namespace
{
    StringTable table()
    {
        StringTable s;
        s.add("SpinnerRideName", u"Spinner");
        s.add("SweetShopStallName", u"Sweets");
        s.add("RideNameZoom", u"Zoom!");
        s.add("TT_AttractionInstance_UnnamedAttraction", u"Nameless");
        s.add("RideRating0", u"Low");
        s.add("RideRating1", u"Medium");
        s.add("RideRating6", u"Off the scale");
        s.add("AttractionStatusOpen", u"Open");
        s.add("AttractionStatusClosed", u"Closed");
        s.add("AttractionFinancesDialogLabel_TotalProfit", u"Profit");
        s.add("AttractionResultsDialogLabel_NotAssessed", u"Not yet");
        s.add("AttractionResultsDialogLabel_Excitement", u"Fun");
        s.add("AttractionResultsDialogLabel_MaxSpeed", u"Fastest");
        s.add("RideMaintenanceDialogLabel_Reliability", u"Sound");
        return s;
    }

    bool has(const std::vector<AttractionTabInfo>& tabs, AttractionTab tab)
    {
        return std::ranges::any_of(tabs, [&](const AttractionTabInfo& t) { return t.tab == tab; });
    }
} // namespace

TEST_CASE("each kind of attraction has its own tabs, in the order of their places on the ring")
{
    const auto tracked = attraction_tabs(AttractionKind::Tracked, save::AttractionState::Open);
    CHECK(tracked.size() == 11);
    CHECK(has(tracked, AttractionTab::Graphs));
    CHECK(std::ranges::is_sorted(tracked, { }, &AttractionTabInfo::slot));
    const auto flat = attraction_tabs(AttractionKind::Flat, save::AttractionState::Closed);
    CHECK(flat.size() == 9);
    CHECK_FALSE(has(flat, AttractionTab::Construction));
    const auto facility = attraction_tabs(AttractionKind::Facility, save::AttractionState::Closed);
    CHECK(facility.size() == 5);
    const auto stall = attraction_tabs(AttractionKind::Stall, save::AttractionState::Closed);
    CHECK(has(stall, AttractionTab::Items));
    CHECK_FALSE(has(stall, AttractionTab::Tests));
    // The open-or-close tab shows the state.
    const auto state = [](const std::vector<AttractionTabInfo>& tabs) { return std::ranges::find(tabs, AttractionTab::State, &AttractionTabInfo::tab)->icon; };
    CHECK(state(tracked) != state(flat));
}

TEST_CASE("an attraction's name is its own, a named string, or its kind and number")
{
    const StringTable s = table();
    save::AttractionCommon a;
    a.name = "Loop the loop";
    CHECK(attraction_name(a, s) == u"Loop the loop");
    a.name = "#RideNameZoom#";
    CHECK(attraction_name(a, s) == u"Zoom!");
    a.name = "%ME% 03";
    a.symbol = "spinnerride:anr";
    CHECK(attraction_name(a, s) == u"Spinner 3");
    a.symbol = "SweetShopstall:sta";
    a.name = "%ME% 01";
    CHECK(attraction_name(a, s) == u"Sweets 1");
    a.symbol = "Unknown:sta";
    CHECK(attraction_name(a, s) == u"Nameless");
}

TEST_CASE("ratings are put in words by how high they are")
{
    const StringTable s = table();
    CHECK(rating_word(0.5f, s) == u"Low");
    CHECK(rating_word(3, s) == u"Medium");
    CHECK(rating_word(99, s) == u"Off the scale");
}

TEST_CASE("pages are written from the attraction's state")
{
    const StringTable s = table();
    save::Ride ride;
    ride.base.name = "Ride";
    ride.base.state = save::AttractionState::Open;
    ride.base.total_profit = 1234;
    const AttractionFacts f { &ride.base, &ride, nullptr };
    const InfoUnits units;

    const InfoPage view = attraction_page(AttractionTab::View, f, s, units);
    CHECK(view.title == u"Ride");
    CHECK(view.note == u"Open");

    const InfoPage money = attraction_page(AttractionTab::Finances, f, s, units);
    const auto profit = std::ranges::find(money.lines, std::u16string(u"Profit"), &InfoLine::label);
    REQUIRE(profit != money.lines.end());
    CHECK(profit->value == money_text(units.currency, 1234));

    InfoPage tests = attraction_page(AttractionTab::Tests, f, s, units);
    CHECK(tests.lines.empty());
    CHECK(tests.note == u"Not yet");
    ride.assessment.valid = true;
    ride.assessment.excitement = 0.25f;
    save::TestResults lap;
    lap.valid = true;
    lap.max_speed = 10;
    ride.stats.last_lap = lap;
    tests = attraction_page(AttractionTab::Tests, f, s, units);
    CHECK(tests.lines[0].value == u"0.25 (Low)");
    tests = attraction_page(AttractionTab::Tests, f, s, units, 1);
    const auto fastest = std::ranges::find(tests.lines, std::u16string(u"Fastest"), &InfoLine::label);
    REQUIRE(fastest != tests.lines.end());
    CHECK(fastest->value == u"36.0 km/h");
    InfoUnits imperial;
    imperial.distance = DistanceUnits::Imperial;
    const InfoPage slow = attraction_page(AttractionTab::Tests, f, s, imperial, 1);
    CHECK(std::ranges::find(slow.lines, std::u16string(u"Fastest"), &InfoLine::label)->value == u"22.4 mph");

    ride.breakdown.reliability = 87.4f;
    CHECK(attraction_page(AttractionTab::Maintenance, f, s, units).lines[0].value == u"87 %");
}

TEST_CASE("rides can be tested as well as opened and closed, other attractions only opened and closed")
{
    CHECK(state_choices(AttractionKind::Tracked).size() == 3);
    CHECK(state_choices(AttractionKind::Flat).size() == 3);
    const auto stall = state_choices(AttractionKind::Stall);
    REQUIRE(stall.size() == 2);
    CHECK(stall[0].state == save::AttractionState::Open);
    CHECK(stall[1].state == save::AttractionState::Closed);
    CHECK(state_choices(AttractionKind::Facility).size() == 2);
}

TEST_CASE("a tracked ride's test results come in sections, a flat ride's in one")
{
    StringTable s = table();
    s.add("RideTest_Force", u"Forces");
    s.add("AttractionResultsDialogLabel_MaxLateralG", u"Sideways");
    save::Ride ride;
    ride.kind = save::RideKind::Tracked;
    save::TestResults lap;
    lap.valid = true;
    lap.max_lateral = 9.81f * 2;
    ride.stats.last_lap = lap;
    const AttractionFacts f { &ride.base, &ride, nullptr };
    const InfoPage forces = attraction_page(AttractionTab::Tests, f, s, { }, 2);
    CHECK(forces.sections.size() == 4);
    CHECK(forces.section == 2);
    CHECK(forces.sections[2] == u"Forces");
    CHECK(std::ranges::find(forces.lines, std::u16string(u"Sideways"), &InfoLine::label)->value == u"2.00 g");
    // Asking past the last section shows the last.
    CHECK(attraction_page(AttractionTab::Tests, f, s, { }, 9).section == 3);

    ride.kind = save::RideKind::Flat;
    CHECK(attraction_page(AttractionTab::Tests, f, s, { }, 2).sections.empty());
}

TEST_CASE("vehicles, operating mode and graphs are written from the ride")
{
    StringTable s = table();
    s.add("AttractionVehicleDialogLabelNumTrains", u"Trains");
    s.add("RideOpModeDialog_OpModeShuttle", u"Back and forth");
    s.add("RideOpModeDialog_MinWaitTime", u"Least wait");
    s.add("Graph_Speed", u"Speed");
    save::Ride ride;
    ride.kind = save::RideKind::Tracked;
    ride.base.symbol = "SpinnerRide:trr";
    ride.trains.resize(3);
    ride.mode = save::OperatingMode::Shuttle;
    ride.options.min_wait = 12;
    const AttractionFacts f { &ride.base, &ride, nullptr };

    const InfoPage vehicles = attraction_page(AttractionTab::Vehicles, f, s, { });
    CHECK(vehicles.heading == u"Spinner");
    const auto trains = std::ranges::find(vehicles.lines, std::u16string(u"Trains"), &InfoLine::label);
    REQUIRE(trains != vehicles.lines.end());
    CHECK(trains->value == u"3");

    const InfoPage mode = attraction_page(AttractionTab::Mode, f, s, { });
    CHECK(mode.heading == u"Back and forth");
    CHECK(mode.sections.size() == 2);
    const InfoPage waits = attraction_page(AttractionTab::Mode, f, s, { }, 1);
    REQUIRE(waits.controls.size() == 5);
    CHECK(waits.controls[0].id == control_id(AttractionControl::WaitFor));
    CHECK(waits.controls[0].items.size() == 5);
    CHECK(waits.controls[0].value == 4); // Any load, the list's last
    CHECK(waits.controls[1].label == u"Least wait");
    CHECK(waits.controls[1].value == 1);
    CHECK(waits.controls[2].value == 12);
    CHECK(waits.controls[2].enabled);
    CHECK(waits.controls[3].value == 0); // No longest wait: unticked, its seconds greyed
    CHECK_FALSE(waits.controls[4].enabled);

    CHECK(attraction_page(AttractionTab::Graphs, f, s, { }).graph.empty());
    ride.graphs.speed = { 1, 2, 3 };
    const InfoPage graphs = attraction_page(AttractionTab::Graphs, f, s, { });
    REQUIRE(graphs.graph.size() == 4);
    CHECK(graphs.graph[0].name == u"Speed");
    CHECK(graphs.graph[0].values.size() == 3);
}

TEST_CASE("a flat ride's operating mode page sets how many times round it runs, and has no waits")
{
    StringTable s = table();
    s.add("RideOpModeDialog_NumCircuits", u"Circuits");
    save::Ride ride;
    ride.kind = save::RideKind::Flat;
    ride.loop_count = 0; // Never set: runs once
    const AttractionFacts f { &ride.base, &ride, nullptr };
    const InfoPage page = attraction_page(AttractionTab::Mode, f, s, { });
    CHECK(page.sections.empty());
    REQUIRE(page.controls.size() == 1);
    CHECK(page.controls[0].id == control_id(AttractionControl::Circuits));
    CHECK(page.controls[0].label == u"Circuits");
    CHECK(page.controls[0].value == 1);
    CHECK(page.controls[0].minimum == 1);
    CHECK(page.controls[0].maximum == kMostCircuits);
}

TEST_CASE("a show ride's operating mode page sets its show trick by trick")
{
    StringTable s = table();
    s.add("PonyHop", u"Hop");
    s.add("PonySpin", u"Spin");
    save::Ride ride;
    ride.kind = save::RideKind::Flat;
    const save::RideShow show { .ride = 1, .tricks = { { .name = "PonyIdle", .seconds = 5, .available = true, .invented = true }, { .name = "PonyHop", .seconds = 30, .available = true, .invented = true }, { .name = "PonySecret", .seconds = 9, .hidden = true, .available = true, .invented = true }, { .name = "PonySpin", .seconds = 45, .available = true, .invented = true }, { .name = "PonyNew", .seconds = 9, .available = true } }, .order = { 3, 1 } };
    CHECK(show_offers(show) == std::vector<std::uint32_t> { 1, 3 }); // Not the idle one, a hidden one or one not researched
    AttractionFacts f { &ride.base, &ride, nullptr };
    f.show = &show;
    const InfoPage second = attraction_page(AttractionTab::Mode, f, s, { }, 1);
    CHECK(second.sections == std::vector<std::u16string> { u"1", u"2", u"+" });
    REQUIRE(second.lines.size() == 1);
    CHECK(second.lines[0].value == u"1:15");
    REQUIRE(second.controls.size() == 2);
    CHECK(second.controls[0].id == control_id(AttractionControl::ShowTrick));
    CHECK(second.controls[0].items == std::vector<std::u16string> { u"Hop", u"Spin" });
    CHECK(second.controls[0].value == 0);
    CHECK(second.controls[1].id == control_id(AttractionControl::ShowDelete));
    const InfoPage adding = attraction_page(AttractionTab::Mode, f, s, { }, 9);
    CHECK(adding.section == 2);
    REQUIRE(adding.controls.size() == 1); // Nothing to delete
    CHECK(adding.controls[0].value == -1);
}

TEST_CASE("the thoughts page shows the latest thoughts about the attraction first")
{
    StringTable s = table();
    s.add("Thought_ImLost1", u"Lost at %NAME%");
    s.add("Thought_ImTired1", u"Tired");
    save::Ride ride;
    ride.base.name = "Big One";
    ride.thoughts = { { 0, 1, 10, 0 }, { 0, 0, -1, 0 }, { 0, 2, 20, 5 }, { 0, 1, 5, 0 } };
    const AttractionFacts f { &ride.base, &ride, nullptr };
    const InfoPage page = attraction_page(AttractionTab::Thoughts, f, s, { });
    REQUIRE(page.lines.size() == 3);
    CHECK(page.lines[0].label == u"Tired");
    CHECK(page.lines[1].label == u"Lost at Big One");
    CHECK(page.lines[0].value.empty());
}

TEST_CASE("values the save does not hold are shown as missing, and what it holds can be changed")
{
    StringTable s = table();
    s.add("RideInspectionIntervalEvery30Mins", u"Half hourly");
    s.add("AttractionCustomersDialogLabel_Sold", u"%NAME% sold");
    s.add("AttractionCustomersDialogLabel_Customers", u"Per hour");
    save::Ride ride;
    ride.kind = save::RideKind::Flat;
    ride.ticket_price = 150;
    ride.breakdown.inspection_interval = 2;
    const AttractionFacts f { &ride.base, &ride, nullptr, { 3, 4, 5 } };

    const InfoPage money = attraction_page(AttractionTab::Finances, f, s, { });
    REQUIRE(money.controls.size() == 1);
    CHECK(money.controls[0].id == control_id(AttractionControl::Price));
    CHECK(money.controls[0].value == 150);
    CHECK(money.controls[0].decimals == 2);
    CHECK(money.controls[0].maximum == kMostRidePrice);

    const InfoPage people = attraction_page(AttractionTab::Customers, f, s, { });
    CHECK(std::ranges::find(people.lines, std::u16string(u"Per hour"), &InfoLine::label)->value == kMissingValue);

    const InfoPage care = attraction_page(AttractionTab::Maintenance, f, s, { });
    REQUIRE(care.controls.size() == 1);
    CHECK(care.controls[0].items.size() == 7);
    CHECK(care.controls[0].items[2] == u"Half hourly");
    CHECK(care.controls[0].value == 2);

    const InfoPage cars = attraction_page(AttractionTab::Vehicles, f, s, { });
    REQUIRE(cars.controls.size() == 1);
    CHECK(cars.controls[0].kind == InfoControlKind::Paint);
    CHECK(cars.controls[0].paints == std::vector<std::size_t> { 3, 4, 5 });
    CHECK(attraction_page(AttractionTab::View, f, s, { }).controls.empty());

    save::Stall stall;
    stall.base.symbol = "SweetShopStall:shs";
    save::StallItem sweets;
    sweets.price = 90;
    sweets.total_sold = 41;
    stall.items = { sweets, sweets };
    const AttractionFacts shop { &stall.base, nullptr, &stall, { 1 } };
    const InfoPage items = attraction_page(AttractionTab::Items, shop, s, { });
    REQUIRE(items.controls.size() == 2);
    CHECK(items.controls[1].id == control_id(AttractionControl::ItemPrice, 1));
    CHECK(items.controls[1].label == u"Sweets");
    CHECK(items.controls[1].value == 90);
    const InfoPage sold = attraction_page(AttractionTab::Customers, shop, s, { });
    CHECK(std::ranges::find(sold.lines, std::u16string(u"Sweets sold"), &InfoLine::label)->value == u"41");
    CHECK(attraction_page(AttractionTab::View, shop, s, { }).controls.size() == 1);
}

TEST_CASE("a running ride shows its riders an hour, the wait its queue now has and its seats")
{
    StringTable s = table();
    s.add("AttractionCustomersDialogLabel_Customers", u"Per hour");
    s.add("AttractionCustomersDialogLabel_QueueTime", u"Queue");
    s.add("AttractionVehicleDialogLabelCapacity", u"Seats");
    save::Ride ride;
    ride.kind = save::RideKind::Tracked;
    ride.stations.emplace_back().queue_time = 600; // What the save kept, no longer shown
    AttractionFacts f { &ride.base, &ride, nullptr };
    f.flow = RideFlow { .seats = 24, .queued = 60, .per_hour = 720 };
    const auto value = [](const InfoPage& page, std::u16string_view label) { return std::ranges::find(page.lines, std::u16string(label), &InfoLine::label)->value; };

    const InfoPage people = attraction_page(AttractionTab::Customers, f, s, { });
    CHECK(value(people, u"Per hour") == u"720");
    CHECK(value(people, u"Queue") == u"5:00"); // 60 in line at 12 a minute
    CHECK(value(attraction_page(AttractionTab::Vehicles, f, s, { }), u"Seats") == u"24");

    f.flow->per_hour = 0; // Not yet measured
    CHECK(value(attraction_page(AttractionTab::Customers, f, s, { }), u"Queue") == kMissingValue);
    f.flow->queued = 0;
    CHECK(value(attraction_page(AttractionTab::Customers, f, s, { }), u"Queue") == u"0:00");
}
