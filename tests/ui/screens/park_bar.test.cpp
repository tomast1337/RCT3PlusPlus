// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/park_bar.hpp"

#include "ui/desktop.hpp"
#include "ui/game_style.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;

namespace
{
    struct Fixture
    {
        StringTable strings;
        Desktop desktop;
        ParkBar* bar = nullptr;

        Fixture()
        {
            strings.add("Playing_PauseHelp", u"Halt");
            strings.add("WeatherType_Cloudy_TT", u"Wolken");
            strings.add("Toolbar_TT_DayNight", u"Tag und Nacht");
            bar = &desktop.root().add<ParkBar>("bar", game_skins::park_bar_style(kGamePalette), strings);
            desktop.layout({ 0, 0, 1000, 700 });
            desktop.update({ });
        }

        void click(const Widget& w)
        {
            const Point p { (w.rect().x0 + w.rect().x1) / 2, (w.rect().y0 + w.rect().y1) / 2 };
            desktop.update({ p, true });
            desktop.update({ p, false });
        }
    };
} // namespace

TEST_CASE("the bar's date names the month and year, then the time")
{
    StringTable strings;
    strings.add("Month0", u"March");
    strings.add("Year", u"Year");
    CHECK(park_bar_date(strings, 0, 2, 13 * 60 + 5, true) == u"March, Year 2  1:05 pm");
    CHECK(park_bar_date(strings, 0, 2, 13 * 60 + 5, false) == u"March, Year 2  13:05");
}

TEST_CASE("the bar sits centred along the top, its items inside it and in order")
{
    Fixture f;
    const Rect r = f.bar->rect();
    CHECK(r.x0 == doctest::Approx((1000 - park_bar::kWidth) / 2));
    CHECK(r.y0 == 0);
    CHECK(r.height() == park_bar::kHeight);
    float last = r.x0;
    for (const auto& child : f.bar->children())
    {
        const Rect c = child->rect();
        CHECK(c.x0 >= last);
        CHECK(c.x1 <= r.x1);
        CHECK(c.y0 >= r.y0);
        CHECK(c.y1 <= r.y1 - park_bar::kLowerEdge);
        last = c.x1;
    }
    CHECK(f.bar->speed_button(0).tooltip == u"Halt");
}

TEST_CASE("the speed buttons ask for a speed, and the bar shows the one it is given")
{
    Fixture f;
    CHECK_FALSE(f.bar->take_speed());
    f.click(f.bar->speed_button(3));
    CHECK(f.bar->take_speed() == simulation::GameSpeed { simulation::Speed::Fastest, false });
    CHECK_FALSE(f.bar->take_speed());
    CHECK(f.bar->speed_button(3).icon == "Top_SuperFastForward"); // not shown until given

    ParkBarValues v;
    v.speed = { simulation::Speed::Fastest, false };
    f.bar->show(v);
    CHECK(f.bar->speed_button(3).icon == "Top_SuperFastForwardSelected");
    CHECK(f.bar->speed_button(1).icon == "Top_Play");

    // Pause keeps the speed it pauses.
    f.click(f.bar->speed_button(0));
    CHECK(f.bar->take_speed() == simulation::GameSpeed { simulation::Speed::Fastest, true });
    v.speed.paused = true;
    f.bar->show(v);
    CHECK(f.bar->speed_button(0).icon == "Top_PauseSelected");
    CHECK(f.bar->speed_button(3).icon == "Top_SuperFastForward");
}

TEST_CASE("the bar's values are written from the park's state")
{
    StringTable strings;
    strings.add("Month1", u"April");
    strings.add("Year", u"Year");
    strings.add("Cash_Unlimited", u"no limit");
    simulation::ParkState state;
    state.cash = -150075;
    state.guests = 312;
    state.rating = 0.5f;
    state.temperature = 30.2f;
    state.date.month = 1;
    state.date.year = 2;
    state.weather = save::WeatherType::Sunny;
    ParkBarUnits units;
    units.currency = { u"$", u"", u"." };
    const ParkBarValues v = park_bar_values(state, 0.75f, strings, units);
    CHECK(v.money == u"-$1,500.75");
    CHECK(v.guests == u"312");
    CHECK(v.rating == 0.5f);
    CHECK(v.temperature == u"30\u00b0C");
    CHECK(v.date == u"April, Year 3  6:00 pm");
    CHECK(v.weather == save::WeatherType::Sunny);

    state.cash.reset();
    units.fahrenheit = true;
    units.twelve_hour = false;
    const ParkBarValues w = park_bar_values(state, 0.75f, strings, units);
    CHECK(w.money == u"no limit");
    CHECK(w.temperature == u"86\u00b0F");
    CHECK(w.date == u"April, Year 3  18:00");
}

TEST_CASE("the weather shows with its help, and the forecast only when it changes")
{
    Fixture f;
    CHECK_FALSE(f.bar->weather().visible);
    ParkBarValues v;
    v.weather = save::WeatherType::Cloudy;
    v.forecast = save::WeatherType::Cloudy;
    v.date = u"May";
    f.bar->show(v);
    CHECK(f.bar->weather().visible);
    CHECK(f.bar->weather().skin == "IconCloud");
    CHECK(f.bar->weather().tooltip == u"Wolken");
    CHECK_FALSE(f.bar->forecast().visible);
    CHECK(f.bar->date().text == u"May");

    v.forecast = save::WeatherType::Snowstorm;
    f.bar->show(v);
    CHECK(f.bar->forecast().visible);
    CHECK(f.bar->forecast().skin == "IconSnow");
}

TEST_CASE("the rating's help gives its number, then its parts once a week has been rated")
{
    StringTable strings;
    strings.add("Toolbar_TT_ParkRating", u"Rating:");
    strings.add("Rct3pp_RatingHappiness", u"Happy:");
    strings.add("Rct3pp_RatingRides", u"Rides:");
    strings.add("Rct3pp_RatingReliability", u"Up:");
    strings.add("Rct3pp_RatingGuests", u"Guests:");
    strings.add("Rct3pp_RatingLitter", u"Litter:");
    strings.add("Rct3pp_RatingHarder", u"Harder:");
    strings.add("Rct3pp_RatingScore", u"Score:");
    CHECK(park_rating_help(strings, 0.4f, std::nullopt) == u"Rating: 400");

    simulation::RatingParts parts;
    parts.happiness = 300;
    parts.rides = 50;
    parts.reliability = 40;
    parts.guests = 30;
    parts.litter = -20;
    parts.total = 400;
    CHECK(park_rating_help(strings, 0.4f, parts) == u"Rating: 400\nHappy: +300\nRides: +50\nUp: +40\nGuests: +30\nLitter: -20\nScore: 400");
}

TEST_CASE("the day mode button shows only with a mode, and makes room for itself")
{
    Fixture f;
    CHECK_FALSE(f.bar->day_mode_button().visible);
    const float money = f.bar->money().rect().width();

    ParkBarValues v;
    v.day_mode = simulation::DayMode::AlwaysNight;
    f.bar->show(v);
    f.desktop.update({ });
    CHECK(f.bar->day_mode_button().visible);
    CHECK(f.bar->day_mode_button().icon == "GUI2_DayNightCycle_Night");
    CHECK(f.bar->day_mode_button().tooltip == u"Tag und Nacht");
    CHECK(f.bar->day_mode_button().rect().x0 > f.bar->objective_button().rect().x1);
    CHECK(f.bar->speed_button(0).rect().x0 > f.bar->day_mode_button().rect().x1);
    CHECK(f.bar->money().rect().width() < money);

    v.day_mode.reset();
    f.bar->show(v);
    f.desktop.update({ });
    CHECK_FALSE(f.bar->day_mode_button().visible);
    CHECK(f.bar->money().rect().width() == doctest::Approx(money));
}

TEST_CASE("the day mode button opens the modes below it, and picking one asks for it")
{
    Fixture f;
    ParkBarValues v;
    v.day_mode = simulation::DayMode::ParkHours;
    f.bar->show(v);
    f.desktop.update({ });
    CHECK_FALSE(f.bar->day_mode_choice(1).visible);

    f.click(f.bar->day_mode_button());
    f.desktop.update({ });
    const Rect button = f.bar->day_mode_button().rect();
    const Rect day = f.bar->day_mode_choice(1).rect();
    const Rect both = f.bar->day_mode_choice(0).rect();
    const Rect night = f.bar->day_mode_choice(2).rect();
    REQUIRE(f.bar->day_mode_choice(1).visible);
    CHECK(both.y0 >= button.y1); // Below the button, the bar's edge down
    CHECK(day.x1 <= both.x0 + 1);
    CHECK(night.x0 >= both.x1 - 1);
    CHECK_FALSE(f.bar->take_day_mode());

    f.click(f.bar->day_mode_choice(2));
    CHECK(f.bar->take_day_mode() == simulation::DayMode::AlwaysNight);
    CHECK_FALSE(f.bar->take_day_mode());
    CHECK_FALSE(f.bar->day_mode_choice(2).visible);
}

TEST_CASE("a double click on the money asks for the finances")
{
    Fixture f;
    f.click(f.bar->money());
    CHECK_FALSE(f.bar->take_finances());
    f.click(f.bar->money());
    CHECK(f.bar->take_finances());
    CHECK_FALSE(f.bar->take_finances());

    // Two clicks far apart are two single clicks.
    f.desktop.update({ }, UpdateContext { .elapsed = 1 });
    f.click(f.bar->money());
    f.desktop.update({ }, UpdateContext { .elapsed = 1 });
    f.click(f.bar->money());
    CHECK_FALSE(f.bar->take_finances());
}
