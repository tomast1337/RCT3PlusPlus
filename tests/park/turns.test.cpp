// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/turns.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

TEST_CASE("sides count clockwise from the next row, and wrap round")
{
    static_assert(Side::kNextRow.right() == Side::kNextCol);
    static_assert(Side::kColBefore.right() == Side::kNextRow);
    static_assert(Side::kNextRow.left() == Side::kColBefore);
    static_assert(Side::kNextCol.opposite() == Side::kColBefore);
    static_assert(Side::from(-1) == Side::kColBefore);
    static_assert(Side::from(6) == Side::kRowBefore);
    static_assert(Side::kRowBefore.turned(-3) == Side::kColBefore);
    CHECK(kSides[2] == Side::kRowBefore);
}

TEST_CASE("a side steps to the square across it, and a step names its side")
{
    CHECK(Side::kNextRow.beyond({ 4, 7 }) == save::Tile { 4, 8 });
    CHECK(Side::kNextCol.beyond({ 4, 7 }) == save::Tile { 5, 7 });
    CHECK(Side::kRowBefore.beyond({ 4, 7 }) == save::Tile { 4, 6 });
    CHECK(Side::kColBefore.beyond({ 4, 7 }) == save::Tile { 3, 7 });
    for (const Side s : kSides)
        CHECK(Side::towards(s.step()[0], s.step()[1]) == s);
    CHECK_FALSE(Side::towards(1, 1).has_value());
    CHECK_FALSE(Side::towards(0, 0).has_value());
}

TEST_CASE("sides towards the columns are the odd ones, each with its own bit")
{
    CHECK_FALSE(Side::kNextRow.on_columns());
    CHECK(Side::kNextCol.on_columns());
    CHECK_FALSE(Side::kRowBefore.on_columns());
    CHECK(Side::kColBefore.on_columns());
    CHECK(Side::kRowBefore.bit() == 4u);
}

TEST_CASE("a model turn wraps round, and turns a piece's side 0 to the side named")
{
    static_assert(ModelTurn::from(5) == ModelTurn { 1 });
    static_assert(ModelTurn { 3 }.turned(2) == ModelTurn { 1 });
    static_assert(ModelTurn::toward(Side::kRowBefore) == ModelTurn { 2 });
    static_assert(ModelTurn { 7 }.facing() == Side::kColBefore);
}

TEST_CASE("a heading wraps round in eighths, and is a quarter turn or a side only along the grid")
{
    static_assert(Heading8::from(-1) == Heading8 { 7 });
    static_assert(Heading8 { 6 }.turned(3) == Heading8 { 1 });
    static_assert(Heading8 { 1 }.turned(ModelTurn { 3 }) == Heading8 { 7 });
    static_assert(Heading8 { 5 }.reversed() == Heading8 { 1 });
    static_assert(Heading8 { 1 }.eighths_from(Heading8 { 7 }) == 2);
    static_assert(Heading8 { 7 }.eighths_from(Heading8 { 1 }) == -2);
    static_assert(Heading8 { 4 }.eighths_from(Heading8 { 0 }) == -4);
    CHECK(Heading8 { 6 }.quarter_turns() == ModelTurn { 3 });
    CHECK_FALSE(Heading8 { 3 }.quarter_turns().has_value());
    CHECK(Heading8 { 0 }.side() == Side::kNextCol);
    CHECK(Heading8 { 2 }.side() == Side::kRowBefore);
    CHECK(Heading8 { 6 }.side() == Side::kNextRow);
    CHECK_FALSE(Heading8 { 5 }.side().has_value());
    // A quarter turn is two eighths, so turning heading 0 by a turn gives that turn back.
    for (std::int32_t t = 0; t < 4; ++t)
        CHECK(Heading8 { }.turned(ModelTurn { t }).quarter_turns() == ModelTurn { t });
}
