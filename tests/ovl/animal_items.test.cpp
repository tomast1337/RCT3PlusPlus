// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/animal_items.hpp"

#include "record_file.hpp"

#include <doctest/doctest.h>

using namespace rct3::test;
using rct3::ovl::read_animal_toys;
using rct3::ovl::read_shelters;

TEST_CASE("a shelter refers to the scenery item it is placed as")
{
    Pool pool;
    pool.add("Den:asd");
    pool.add("DenHouse:sid");
    std::vector<std::byte> bytes(0x10, std::byte { 0 });
    const auto overlay = make_record_overlay(5, pool, "Den:asd", bytes, { { 0, "DenHouse:sid" } });
    const auto items = read_shelters(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK(items[0].name == "Den");
    CHECK(items[0].scenery == "DenHouse");
}

TEST_CASE("an animal toy has a name, icon, shape and price")
{
    Pool pool;
    pool.add("hoop:wai");
    pool.add("hoop:shs");
    pool.add("HoopIcon:gsi");
    pool.add("HoopName:txt");
    pool.add("HoopDesc:txt");
    std::vector<std::byte> bytes(0x5C, std::byte { 0 });
    std::vector<std::byte> price;
    put(price, { 4200 });
    std::copy(price.begin(), price.end(), bytes.begin() + 0x1C);
    const auto overlay = make_record_overlay(5, pool, "hoop:wai", bytes, { { 0x14, "hoop:shs" }, { 0x38, "HoopIcon:gsi" }, { 0x3C, "HoopName:txt" }, { 0x54, "HoopDesc:txt" } });
    const auto items = read_animal_toys(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].toy.has_value());
    const auto& t = *items[0].toy;
    CHECK(t.name_text == "HoopName");
    CHECK(t.description_text == "HoopDesc");
    CHECK(t.icon == "HoopIcon");
    CHECK(t.shape == "hoop");
    CHECK(t.price == 4200);
}

TEST_CASE("an animal toy cut short is listed without its fields")
{
    Pool pool;
    pool.add("bit:wai");
    std::vector<std::byte> bytes(0x20, std::byte { 0 });
    const auto overlay = make_record_overlay(5, pool, "bit:wai", bytes);
    const auto items = read_animal_toys(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK_FALSE(items[0].toy.has_value());
}
