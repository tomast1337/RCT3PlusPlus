// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/animal_motions.hpp"

#include "record_file.hpp"

#include <doctest/doctest.h>

using namespace rct3::test;
using rct3::ovl::read_animal_motions;

TEST_CASE("an animal motion set names an animation and a length for each slot")
{
    Pool pool;
    pool.add("wombat:wad");
    pool.add("WombatWalk:modelanim");
    pool.add("WombatStand:modelanim");
    const std::uint32_t at = pool.base();
    // The set, then three animation references (the middle one unused), then three lengths.
    std::vector<std::byte> bytes;
    put(bytes, { 3, 1, 3, at + 0x20, at + 0x2C });
    bytes.resize(0x2C, std::byte { 0 });
    put(bytes, { bits(1.5f), 0, bits(2.25f) });
    const auto overlay = make_record_overlay(5, pool, "wombat:wad", bytes, { { 0x20, "WombatWalk:modelanim" }, { 0x28, "WombatStand:modelanim" } });
    const auto items = read_animal_motions(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK(items[0].name == "wombat");
    REQUIRE(items[0].motions.has_value());
    const auto& m = *items[0].motions;
    REQUIRE(m.animations.size() == 3);
    CHECK(m.animations[0] == "WombatWalk");
    CHECK(m.animations[1].empty());
    CHECK(m.animations[2] == "WombatStand");
    REQUIRE(m.seconds.size() == 3);
    CHECK(m.seconds[2] == doctest::Approx(2.25f));
}

TEST_CASE("an animal motion set whose lists leave the data is unreadable")
{
    Pool pool;
    pool.add("lost:wad");
    std::vector<std::byte> bytes;
    put(bytes, { 3, 1, 4, 1u << 30, 1u << 30 });
    const auto overlay = make_record_overlay(5, pool, "lost:wad", bytes);
    const auto items = read_animal_motions(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK_FALSE(items[0].motions.has_value());
}
