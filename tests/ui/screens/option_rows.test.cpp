// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/option_rows.hpp"

#include "ui/desktop.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;
using O = config::GameOption;

namespace
{
    OptionRowsStyle style()
    {
        OptionRowsStyle s;
        s.row_height = 20;
        s.row_gap = 2;
        s.section_gap = 10;
        return s;
    }
} // namespace

TEST_CASE("controls start at their option and write changes straight back")
{
    config::GameOptions options;
    options.set(O::Clouds, 0);
    options.set(O::Shadows, 1);
    Widget page("page");
    OptionRows rows(page, 400, style(), options);
    Tickbox& clouds = rows.tick(O::Clouds, u"Clouds");
    Dropdown& shadows = rows.choice(O::Shadows, u"Shadows", { u"None", u"Static", u"Dynamic" });
    Slider& volume = rows.level(O::MusicVolume, u"Music");
    CHECK_FALSE(clouds.ticked);
    CHECK(shadows.selected() == 1);
    CHECK(volume.value() == doctest::Approx(options.get(O::MusicVolume)));

    CHECK_FALSE(rows.update());
    clouds.on_key({ Key::Space });
    CHECK(rows.update());
    CHECK(options.flag(O::Clouds));
    volume.on_key({ Key::End });
    CHECK(rows.update());
    CHECK(options.get(O::MusicVolume) == doctest::Approx(1));
}

TEST_CASE("rows run down the page, hidden rows take no room")
{
    config::GameOptions options;
    options.set(O::ReflectiveWater, 0);
    Widget page("page");
    OptionRows rows(page, 400, style(), options);
    rows.tick(O::ReflectiveWater, u"Water");
    Tickbox& terrain = rows.tick(O::ReflectTerrain, u"Terrain");
    rows.indent();
    rows.shown_when({ O::ReflectiveWater, 1 });
    rows.section();
    Tickbox& clouds = rows.tick(O::Clouds, u"Clouds");

    rows.update();
    CHECK_FALSE(terrain.visible);
    CHECK(rows.height() == doctest::Approx(20 + 10 + 20));
    CHECK(clouds.placement.offsets.y0 > 20);

    options.set(O::ReflectiveWater, 1);
    rows.update();
    CHECK(terrain.visible);
    CHECK(rows.height() == doctest::Approx(20 + 2 + 20 + 10 + 20));
    // Indented further than the row above.
    CHECK(terrain.placement.offsets.x0 > style().left);
}

TEST_CASE("a row can be usable only while another option is on")
{
    config::GameOptions options;
    options.set(O::AllPhotos, 0);
    Widget page("page");
    OptionRows rows(page, 400, style(), options);
    rows.tick(O::AllPhotos, u"All photos");
    Tickbox& spots = rows.tick(O::PhotoSpotPhotos, u"Photo spots");
    rows.enabled_when({ O::AllPhotos, 1 });
    rows.update();
    CHECK(spots.visible);
    CHECK_FALSE(spots.enabled);
    options.set(O::AllPhotos, 1);
    rows.update();
    CHECK(spots.enabled);
}

TEST_CASE("pairs of sliders write both their options")
{
    config::GameOptions options;
    Widget page("page");
    OptionRows rows(page, 400, style(), options);
    rows.pair(config::kDetailPairs[0], u"People");
    rows.update();
    REQUIRE(page.children().size() == 3); // The name and two sliders
    auto& distance = static_cast<Slider&>(*page.children()[2]);
    distance.on_key({ Key::Home });
    CHECK(rows.update());
    CHECK(options.get(config::kDetailPairs[0].distance) == 0);
    CHECK(options.get(config::kDetailPairs[0].detail) > 0);
}
