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
            bar = &desktop.root().add<ParkBar>("bar", game_skins::park_bar_style(), strings);
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
