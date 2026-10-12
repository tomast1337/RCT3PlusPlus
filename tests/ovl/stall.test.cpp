// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/stall.hpp"

#include "record_file.hpp"

#include <doctest/doctest.h>

using namespace rct3::test;
using rct3::ovl::read_stalls;

TEST_CASE("version 1 stalls start with the attraction fields")
{
    Pool pool;
    pool.add("Burgers:sta");
    pool.add("BurgersName:txt");
    pool.add("BurgersDescription:txt");
    pool.add("BurgersIcon:gsi");
    std::vector<std::byte> bytes;
    put(bytes, { 8, 0, 0, 0, 10000, static_cast<std::uint32_t>(-7500) });
    bytes.resize(0x30, std::byte { 0 });
    const auto overlay = make_record_overlay(1, pool, "Burgers:sta", bytes, { { 4, "BurgersName:txt" }, { 8, "BurgersDescription:txt" }, { 0xC, "BurgersIcon:gsi" } });
    const auto items = read_stalls(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].stall.has_value());
    const auto& i = items[0].stall->info;
    CHECK(i.type_id == 8);
    CHECK(i.name_text == "BurgersName");
    CHECK(i.icon == "BurgersIcon");
    CHECK(i.value_a == 10000);
}

TEST_CASE("later stalls give the scenery item only")
{
    Pool pool;
    pool.add("Burgers:sta");
    pool.add("Burgers:sid");
    std::vector<std::byte> bytes(0x30, std::byte { 0 });
    const auto overlay = make_record_overlay(5, pool, "Burgers:sta", bytes, { { 0x28, "Burgers:sid" } });
    const auto items = read_stalls(overlay, overlay.common());
    REQUIRE(items[0].stall.has_value());
    CHECK(items[0].stall->info.style == "Burgers");
    CHECK(items[0].stall->info.name_text.empty());
}
