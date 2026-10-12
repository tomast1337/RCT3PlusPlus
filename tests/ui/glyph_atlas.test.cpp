// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/glyph_atlas.hpp"

#include <array>
#include <doctest/doctest.h>

using namespace rct3::ui;

TEST_CASE("glyphs fill rows, then new rows, then new pages")
{
    // Page 16: one-pixel border and gaps, so three 4-wide glyphs fit a row (1 + 4 + 1 + 4 + 1 + 4 + 1 = 16).
    const std::array<AtlasSize, 5> sizes = { { { 4, 3 }, { 4, 5 }, { 4, 2 }, { 4, 4 }, { 10, 9 } } };
    const auto slots = pack_atlas(sizes, 16);
    REQUIRE(slots.size() == 5);
    CHECK(slots[0].x == 1);
    CHECK(slots[0].y == 1);
    CHECK(slots[1].x == 6);
    CHECK(slots[2].x == 11);
    // The second row starts below the tallest glyph of the first.
    CHECK(slots[3].x == 1);
    CHECK(slots[3].y == 7);
    CHECK(slots[3].page == 0);
    // Rows 1..6 and 7..10 are taken; 9 more rows do not fit.
    CHECK(slots[4].page == 1);
    CHECK(slots[4].x == 1);
    CHECK(slots[4].y == 1);
    CHECK(atlas_page_count(slots) == 2);
}

TEST_CASE("empty and oversized glyphs take no space")
{
    const std::array<AtlasSize, 3> sizes = { { { 0, 5 }, { 15, 2 }, { 3, 3 } } };
    const auto slots = pack_atlas(sizes, 16);
    CHECK(slots[0].page == 0);
    CHECK(slots[1].page == static_cast<std::uint32_t>(-1));
    CHECK(slots[2].x == 1);
    CHECK(slots[2].y == 1);
    CHECK(atlas_page_count(slots) == 1);
}

TEST_CASE("pages are white with coverage in alpha")
{
    const std::array<CoverageBitmap, 1> bitmaps = { { { 2, 1, { 200, 50 } } } };
    const std::array<AtlasSlot, 1> slots = { { { 0, 1, 2 } } };
    const auto pages = atlas_pages(bitmaps, slots, 4);
    REQUIRE(pages.size() == 1);
    REQUIRE(pages[0].pixels.size() == 4 * 4 * 4);
    const auto at = [&](std::uint32_t x, std::uint32_t y, std::uint32_t c) { return pages[0].pixels[((y * 4) + x) * 4 + c]; };
    CHECK(at(1, 2, 3) == 200);
    CHECK(at(2, 2, 3) == 50);
    CHECK(at(0, 0, 3) == 0);
    CHECK(at(0, 0, 0) == 255);
    CHECK(at(1, 2, 2) == 255);
}
