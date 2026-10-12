// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/font_sets.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

TEST_CASE("the font set follows the interface's width")
{
    CHECK(font_set_width(640) == 800);
    CHECK(font_set_width(800) == 800);
    CHECK(font_set_width(800.5f) == 1024);
    CHECK(font_set_width(1024) == 1024);
    CHECK(font_set_width(1025) == 1280);
    CHECK(font_set_width(3840) == 1280);
}

TEST_CASE("languages with their own fonts have their own sets")
{
    CHECK(font_overlay("German", 1024) == "gui/1024/Resolution.common.ovl");
    CHECK(font_overlay("SChinese", 800) == "gui/800/SChinese.common.ovl");
    CHECK(font_overlay("Unknown", 1280) == "gui/1280/Resolution.common.ovl");
}

TEST_CASE("a set's fonts fill roles by name")
{
    CHECK(set_font_role("GUIFontScaledMedium") == FontRole::Medium);
    CHECK(set_font_role("GUIFontScaledAllLanguages") == FontRole::AllScripts);
    CHECK_FALSE(set_font_role("GUIFontMedium"));
}
