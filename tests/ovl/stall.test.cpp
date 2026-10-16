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

TEST_CASE("later stalls point to their goods and attraction fields")
{
    Pool pool;
    pool.add("Burgers:sta");
    pool.add("Burgers:sid");
    pool.add("Burger:cid");
    pool.add("BigBurger:cid");
    pool.add("BurgersName:txt");
    const std::uint32_t at = pool.base();
    // Two goods from 0x2C, then the attraction fields.
    std::vector<std::byte> bytes;
    put(bytes, { ~0u, 2, at + 0x2C, 0, 0, 0, 0, 0, 0, at + 0x3C, 0 });
    put(bytes, { 0, 150, 0, 275 });
    put(bytes, { 768 + 7, 0, 0, 0, 10000 });
    bytes.resize(0x3C + 0x28, std::byte { 0 });
    const auto overlay = make_record_overlay(5, pool, "Burgers:sta", bytes, { { 0x28, "Burgers:sid" }, { 0x2C, "Burger:cid" }, { 0x34, "BigBurger:cid" }, { 0x40, "BurgersName:txt" } });
    const auto items = read_stalls(overlay, overlay.common());
    REQUIRE(items[0].stall.has_value());
    const auto& s = *items[0].stall;
    CHECK(s.info.style == "Burgers");
    CHECK(s.info.type_id == 7);
    CHECK(s.info.name_text == "BurgersName");
    CHECK(s.info.value_a == 10000);
    REQUIRE(s.goods.size() == 2);
    CHECK(s.goods[0].item == "Burger");
    CHECK(s.goods[0].price == 150);
    CHECK(s.goods[1].item == "BigBurger");
    CHECK(s.goods[1].price == 275);
}

TEST_CASE("a later stall with null pointers gives the scenery item only")
{
    Pool pool;
    pool.add("Burgers:sta");
    pool.add("Burgers:sid");
    std::vector<std::byte> bytes;
    put(bytes, { ~0u, 3 });
    bytes.resize(0x30, std::byte { 0 });
    const auto overlay = make_record_overlay(5, pool, "Burgers:sta", bytes, { { 0x28, "Burgers:sid" } });
    const auto items = read_stalls(overlay, overlay.common());
    REQUIRE(items[0].stall.has_value());
    CHECK(items[0].stall->info.style == "Burgers");
    CHECK(items[0].stall->info.name_text.empty());
    CHECK(items[0].stall->goods.empty());
}

TEST_CASE("later stalls give their type after the things they sell")
{
    Pool pool;
    pool.add("Drinks:sta");
    std::vector<std::byte> bytes(0x2C, std::byte { 0 });
    bytes[0x04] = std::byte { 2 }; // Two things sold, two (item, price) pairs follow
    put(bytes, { 0, 150, 0, 200 });
    put(bytes, { 8 + 768 });
    const auto overlay = make_record_overlay(5, pool, "Drinks:sta", bytes, { });
    const auto items = read_stalls(overlay, overlay.common());
    REQUIRE(items[0].stall.has_value());
    CHECK(items[0].stall->info.type_id == 8);
}

TEST_CASE("a later stall whose count runs past its block has no type")
{
    Pool pool;
    pool.add("Drinks:sta");
    std::vector<std::byte> bytes(0x30, std::byte { 0 });
    bytes[0x04] = std::byte { 40 };
    const auto overlay = make_record_overlay(4, pool, "Drinks:sta", bytes, { });
    const auto items = read_stalls(overlay, overlay.common());
    REQUIRE(items[0].stall.has_value());
    CHECK(items[0].stall->info.type_id == 0);
}
