// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/support_sets.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

TEST_CASE("support pieces gather by group and role, middles tallest first")
{
    SupportSets sets;
    add_support_piece(sets, "stone", kSupportMiddleType, "Post2", 2);
    add_support_piece(sets, "stone", kSupportMiddleType, "Post8", 8);
    add_support_piece(sets, "stone", kSupportMiddleType, "Post1", 1);
    add_support_piece(sets, "stone", kSupportMiddleType, "OtherPost2", 2); // A second of a height is ignored
    add_support_piece(sets, "stone", kSupportFootType, "Foot", 1);
    add_support_piece(sets, "stone", kSupportDeepFootType, "DeepFoot", 1);
    add_support_piece(sets, "stone", kSupportTopType, "Head", 1);
    add_support_piece(sets, "stone", kSupportCapType, "Hat", 1);
    add_support_piece(sets, "stone", kSupportBeamType, "Beam", 1);
    add_support_piece(sets, "stone", 19, "Deck", 1); // Not a support piece
    add_support_piece(sets, "", kSupportMiddleType, "Loose", 1);
    REQUIRE(sets.size() == 1);
    REQUIRE(sets.at("stone").size() == 1);
    const SupportSet& s = sets.at("stone").front();
    REQUIRE(s.middles.size() == 3);
    CHECK(s.middles[0].symbol == "Post8");
    CHECK(s.middles[1].symbol == "Post2");
    CHECK(s.middles[2].symbol == "Post1");
    CHECK(s.foot->symbol == "Foot");
    CHECK(s.deep_foot->symbol == "DeepFoot");
    CHECK(s.top->symbol == "Head");
    CHECK(s.cap->symbol == "Hat");
    REQUIRE(s.beams.size() == 1);
    CHECK_FALSE(s.stands_on_anything);
}

TEST_CASE("a group keeps a set per facing, and finds one turned to face the way asked")
{
    SupportSets sets;
    add_support_piece(sets, "wood", kSupportMiddleType, "Round", 1, kRoundSupport);
    add_support_piece(sets, "wood", kSupportMiddleType, "Slant", 1, kFirstDiagonal, kStandsClose);
    add_support_piece(sets, "wood", kSupportMiddleType, "Flat", 1, kAlongX);
    REQUIRE(sets.at("wood").size() == 3);

    const FoundSet slant = find_support_set(sets, "wood", kFirstDiagonal + 3);
    REQUIRE(slant.set != nullptr);
    CHECK(slant.set->middles.front().symbol == "Slant");
    CHECK(slant.quarter_turns == 3);
    CHECK(slant.set->stands_on_anything);
    CHECK(slant.set->stands_close);

    const FoundSet flat = find_support_set(sets, "wood", kAlongZ);
    CHECK(flat.set->middles.front().symbol == "Flat");
    CHECK(flat.quarter_turns == 1);
    CHECK(find_support_set(sets, "wood", kAlongX).quarter_turns == 0);
    CHECK(find_support_set(sets, "wood", kRoundSupport).set->middles.front().symbol == "Round");
    CHECK(find_support_set(sets, "metal", kRoundSupport).set == nullptr);

    SupportSets only_flat;
    add_support_piece(only_flat, "plate", kSupportMiddleType, "Plate", 1, kAlongX);
    CHECK(find_support_set(only_flat, "plate", kRoundSupport).set != nullptr); // Any set rather than none
}
