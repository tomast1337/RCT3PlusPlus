// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/map_panel.hpp"

#include "ui/game_style.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

TEST_CASE("legend rows stack, with a gap above each that starts a group")
{
    MapPanelStyle style;
    style.row_height = 20;
    style.group_gap = 8;
    std::vector<MapLegendEntry> entries(4);
    entries[0].starts_group = true; // The first row needs no gap
    entries[2].starts_group = true;
    const std::vector<float> tops = legend_tops(style, entries);
    REQUIRE(tops.size() == 4);
    CHECK(tops[0] == 0);
    CHECK(tops[1] == 20);
    CHECK(tops[2] == 48);
    CHECK(tops[3] == 68);
}

TEST_CASE("the legend fits in the smallest window")
{
    MapPanelStyle style;
    style.window = game_skins::window_style(kGamePalette);
    std::vector<MapLegendEntry> entries(16);
    for (const std::size_t i : { 4, 6, 14 })
        entries[i].starts_group = true;
    const float legend = legend_tops(style, entries).back() + style.row_height;
    CHECK(legend + style.window.content_inset.y0 + style.window.content_inset.y1 <= style.min_size.y);
}
