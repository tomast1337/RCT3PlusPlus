// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/support_footprint.hpp"

#include "ovl/scenery_item.hpp"

#include <bit>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{

    ovl::SceneryItem item(std::uint32_t flags, std::uint32_t type = 18)
    {
        ovl::SceneryItem i;
        i.flags = flags;
        i.type = type;
        i.x_squares = 2;
        i.z_squares = 1;
        const auto square = [&](std::uint32_t flags, std::int32_t low, std::int32_t high, SupportKind kind) {
            ovl::SceneryItemSquare& s = i.squares.emplace_back();
            s.flags = flags;
            s.min_height = low;
            s.max_height = high;
            s.support_kind = kind;
        };
        square(0x2 | (0x10u << 3), 1, 3, kAlongX);
        square(0x1, 0, 2, kNoSupport);
        return i;
    }

} // namespace

TEST_CASE("a footprint keeps each square's levels, facing and filled thirds")
{
    const auto f = support_footprint(item(0x80 | 0x200));
    REQUIRE(f);
    CHECK(f->wants_supports);
    CHECK(f->alternate_tiles);
    CHECK(f->track);
    CHECK_FALSE(f->by_points);
    CHECK(f->x_squares == 2);
    REQUIRE(f->squares.size() == 2);
    CHECK(f->squares[0].low == 1);
    CHECK(f->squares[0].high == 3);
    CHECK(f->squares[0].kind == kAlongX);
    CHECK(f->squares[0].wants_column);
    CHECK(f->squares[0].cells == 0x10); // The middle third
    CHECK_FALSE(f->squares[1].wants_column);
}

TEST_CASE("support points are read from the item's point records")
{
    ovl::SceneryItem i = item(0x80 | 0x400 | 0x800 | 0x100);
    ovl::SceneryItemNode n;
    n.words = { 1, 0, 2, 1, std::bit_cast<std::uint32_t>(0.25f), std::bit_cast<std::uint32_t>(0.75f), std::bit_cast<std::uint32_t>(4.5f), static_cast<std::uint32_t>(-1), 0, 0, 2, std::bit_cast<std::uint32_t>(3.0f) };
    n.words[20] = 0x3;
    n.words[21] = 4;
    i.nodes.push_back(n);
    const auto f = support_footprint(i);
    REQUIRE(f);
    CHECK(f->by_points);
    CHECK(f->capped);
    CHECK(f->carries_columns);
    REQUIRE(f->points.size() == 1);
    const SupportPoint& p = f->points[0];
    CHECK(p.top.square == std::array { 1, 0 });
    CHECK(p.top.cell == std::array { 2, 1 });
    CHECK(p.top.within[1] == 0.75f);
    CHECK(p.top_height == 4.5f);
    CHECK(p.column.square == std::array { -1, 0 }); // Beside the item
    CHECK(p.column.cell == std::array { 0, 2 });
    CHECK(p.column_height == 3.0f);
    CHECK(p.hanging);
    CHECK(p.straight_only);
    CHECK(p.group == 4);
}

TEST_CASE("every item with squares has a footprint, as columns stop at it, but joined coaster pieces do not")
{
    CHECK(support_footprint(item(0x1, 3)));
    CHECK(support_footprint(item(0x100, 3)));
    CHECK_FALSE(support_footprint(item((1u << 26) | 0x1, 3)));
}

TEST_CASE("column facings turn with the item")
{
    CHECK(turned(kRoundSupport, 1) == kRoundSupport);
    CHECK(turned(kAlongX, 1) == kAlongZ);
    CHECK(turned(kAlongZ, 2) == kAlongZ);
    CHECK(turned(5, 1) == 2);
    CHECK(turned(3, -1) == 2);
    CHECK(turned(kNoSupport, 3) == kNoSupport);
}
