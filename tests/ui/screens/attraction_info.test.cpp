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
    REQUIRE_FALSE(money.lines.empty());
    CHECK(money.lines[0].label == u"Profit");
    CHECK(money.lines[0].value == money_text(units.currency, 1234));

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
    const auto fastest = std::ranges::find(tests.lines, std::u16string(u"Fastest"), &InfoLine::label);
    REQUIRE(fastest != tests.lines.end());
    CHECK(fastest->value == u"36.0 km/h");
    InfoUnits imperial;
    imperial.distance = DistanceUnits::Imperial;
    const InfoPage slow = attraction_page(AttractionTab::Tests, f, s, imperial);
    CHECK(std::ranges::find(slow.lines, std::u16string(u"Fastest"), &InfoLine::label)->value == u"22.4 mph");

    ride.breakdown.reliability = 87.4f;
    CHECK(attraction_page(AttractionTab::Maintenance, f, s, units).lines[0].value == u"87 %");
}
