// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/enclosure_fence.hpp"

#include "record_file.hpp"

#include <doctest/doctest.h>

using namespace rct3::test;
using rct3::ovl::read_enclosure_fences;

TEST_CASE("an enclosure fence names its pieces and costs")
{
    Pool pool;
    const std::array<std::uint32_t, 9> pieces = { pool.add("RopeFull"), pool.add("RopeEnd"), pool.add("RopeCorner"), pool.add("RopeEdge"), pool.add("RopeInner"), pool.add("RopeGate"), pool.add("RopeLookLeft"), pool.add("RopeLook"), pool.add("RopeLookRight") };
    pool.add("Rope:enc");
    pool.add("RopeName:txt");
    pool.add("RopeIcon:gsi");
    std::vector<std::byte> bytes;
    put(bytes, { 0, 0, 0 });
    for (const std::uint32_t p : pieces)
        put(bytes, { p });
    put(bytes, { 700, static_cast<std::uint32_t>(-600), 3000, static_cast<std::uint32_t>(-2500) });
    put(bytes, { 0, 0, 0, 0, bits(2.5f) });
    bytes.resize(0x5C, std::byte { 0 });
    const auto overlay = make_record_overlay(5, pool, "Rope:enc", bytes, { { 4, "RopeName:txt" }, { 8, "RopeIcon:gsi" } });
    const auto items = read_enclosure_fences(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK(items[0].name == "Rope");
    REQUIRE(items[0].fence.has_value());
    const auto& f = *items[0].fence;
    CHECK(f.name_text == "RopeName");
    CHECK(f.icon == "RopeIcon");
    CHECK(f.pieces[0] == "RopeFull");
    CHECK(f.pieces[5] == "RopeGate");
    CHECK(f.pieces[8] == "RopeLookRight");
    CHECK(f.fence_cost == 700);
    CHECK(f.fence_refund == -600);
    CHECK(f.gate_cost == 3000);
    CHECK(f.gate_refund == -2500);
    CHECK(f.strength == doctest::Approx(2.5f));
}

TEST_CASE("an enclosure fence cut short is listed without its fields")
{
    Pool pool;
    pool.add("Stub:enc");
    std::vector<std::byte> bytes(0x10, std::byte { 0 });
    const auto overlay = make_record_overlay(5, pool, "Stub:enc", bytes);
    const auto items = read_enclosure_fences(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK_FALSE(items[0].fence.has_value());
}
