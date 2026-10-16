// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/animal_species.hpp"

#include "record_file.hpp"

#include <doctest/doctest.h>

using namespace rct3::test;
using rct3::ovl::read_animal_species;

TEST_CASE("an animal species points to its animals and names its shelter")
{
    Pool pool;
    const std::uint32_t shelter = pool.add("burrow");
    pool.add("wombat:was");
    pool.add("WombatName:txt");
    pool.add("WombatDesc:txt");
    pool.add("WombatIcon:gsi");
    pool.add("BigWombat:txt");
    pool.add("SmallWombat:txt");
    pool.add("WombatAdult:gsi");
    pool.add("WombatYoung:gsi");
    const std::uint32_t at = pool.base();
    // The species, then two animal blocks; the female slots are null.
    std::vector<std::byte> bytes;
    put(bytes, { 0, 0, 0, 0, 0, 0, at + 0x40, 0, at + 0x74, 0, 0, 0, shelter, 0, bits(1.5f) });
    bytes.resize(0x40, std::byte { 0 });
    for (const std::uint32_t price : { 12500u, 16000u })
    {
        const std::size_t start = bytes.size();
        bytes.resize(start + 0x34, std::byte { 0 });
        std::vector<std::byte> p;
        put(p, { price, bits(0.7f) });
        std::copy(p.begin(), p.end(), bytes.begin() + static_cast<std::ptrdiff_t>(start + 0x14));
    }
    const auto overlay = make_record_overlay(5, pool, "wombat:was", bytes, { { 8, "WombatName:txt" }, { 0xC, "WombatDesc:txt" }, { 0x10, "WombatIcon:gsi" }, { 0x40 + 0x1C, "BigWombat:txt" }, { 0x40 + 0x30, "WombatAdult:gsi" }, { 0x74 + 0x1C, "SmallWombat:txt" }, { 0x74 + 0x30, "WombatYoung:gsi" } });
    const auto items = read_animal_species(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK(items[0].name == "wombat");
    REQUIRE(items[0].species.has_value());
    const auto& s = *items[0].species;
    CHECK(s.name_text == "WombatName");
    CHECK(s.description_text == "WombatDesc");
    CHECK(s.icon == "WombatIcon");
    CHECK(s.shelter == "burrow");
    CHECK(s.fence_strength == doctest::Approx(1.5f));
    REQUIRE(s.variants[0].has_value());
    CHECK(s.variants[0]->description_text == "BigWombat");
    CHECK(s.variants[0]->icon == "WombatAdult");
    CHECK(s.variants[0]->price == 12500);
    CHECK(s.variants[0]->rarity == doctest::Approx(0.7f));
    CHECK_FALSE(s.variants[1].has_value());
    REQUIRE(s.variants[2].has_value());
    CHECK(s.variants[2]->description_text == "SmallWombat");
    CHECK(s.variants[2]->price == 16000);
    CHECK_FALSE(s.variants[3].has_value());
}

TEST_CASE("an animal pointer outside the data leaves that animal out")
{
    Pool pool;
    pool.add("lost:was");
    std::vector<std::byte> bytes;
    put(bytes, { 0, 0, 0, 0, 0, 0, 1u << 30 });
    bytes.resize(0x3C, std::byte { 0 });
    const auto overlay = make_record_overlay(5, pool, "lost:was", bytes);
    const auto items = read_animal_species(overlay, overlay.common());
    REQUIRE(items[0].species.has_value());
    CHECK_FALSE(items[0].species->variants[0].has_value());
    CHECK(items[0].species->shelter.empty());
}
