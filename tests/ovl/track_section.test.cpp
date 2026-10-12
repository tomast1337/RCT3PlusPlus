// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/track_section.hpp"

#include "record_file.hpp"

#include <doctest/doctest.h>

using namespace rct3::test;
using rct3::ovl::read_track_sections;

namespace
{

    constexpr std::uint32_t kSize[] = { 0, 228, 0, 0, 364, 392 };

    // A record of `words` 32-bit fields with the given pointer and number fields filled in.
    std::vector<std::byte> record(std::uint32_t version, Pool& pool)
    {
        std::vector<std::uint32_t> w(kSize[version] / 4, 0);
        w[0] = pool.add("Curve");
        w[2] = 3;
        w[3] = 6;
        w[6] = 0x400004;
        w[7] = 0x400002;
        w[0x88 / 4] = bits(1.0f);
        w[0xBC / 4] = bits(1.0f);
        w[0xC0 / 4] = bits(2.0f);
        w[0xC4 / 4] = bits(3.0f);
        std::vector<std::byte> out;
        for (auto v : w)
            append_u32(out, v);
        return out;
    }

} // namespace

TEST_CASE("track sections are read in every file version")
{
    for (std::uint32_t version : { 1u, 4u, 5u })
    {
        CAPTURE(version);
        Pool pool;
        pool.add("Curve");
        pool.add("Curve:tks");
        pool.add("Curve:sid");
        pool.add("CurveL:spl");
        pool.add("CurveR:spl");
        pool.add("Extra:spl");
        const auto bytes = record(version, pool);
        const auto overlay = make_record_overlay(version, pool, "Curve:tks", bytes, { { 4, "Curve:sid" }, { 0x20, "CurveL:spl" }, { 0x24, "CurveR:spl" }, { 0x28, "CurveL:spl" }, { 0x2C, "CurveR:spl" }, { 0x30, "Extra:spl" } });
        const auto items = read_track_sections(overlay, overlay.common());
        REQUIRE(items.size() == 1);
        CHECK(items[0].name == "Curve");
        REQUIRE(items[0].section.has_value());
        const auto& s = *items[0].section;
        CHECK(s.name == "Curve");
        CHECK(s.scenery_item == "Curve");
        CHECK(s.join_a == 3);
        CHECK(s.join_b == 6);
        CHECK(s.flags[0] == 0x400004);
        CHECK(s.flags[1] == 0x400002);
        CHECK(s.splines[0] == "CurveL");
        CHECK(s.splines[1] == "CurveR");
        CHECK(s.splines[3] == "CurveR");
        CHECK(s.extra_splines[0] == "Extra");
        CHECK(s.extra_splines[1].empty());
        CHECK(s.scale[1] == 2.0f);
        CHECK(s.references.size() == 6);
        CHECK(s.references[0].offset == 4);
        CHECK(s.references[0].tag == "sid");
        CHECK(s.record.size() == kSize[version]);
    }
}

TEST_CASE("a track section that does not fit its block is unreadable")
{
    Pool pool;
    pool.add("Curve:tks");
    std::vector<std::byte> bytes;
    put(bytes, { 0, 0, 0, 0 }); // far shorter than a record
    const auto overlay = make_record_overlay(5, pool, "Curve:tks", bytes);
    const auto items = read_track_sections(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK_FALSE(items[0].section.has_value());
}

TEST_CASE("references of the wrong type are not used")
{
    Pool pool;
    pool.add("Curve:tks");
    pool.add("Curve:ftx");
    std::vector<std::byte> bytes(392, std::byte { 0 });
    const auto overlay = make_record_overlay(5, pool, "Curve:tks", bytes, { { 4, "Curve:ftx" } });
    const auto items = read_track_sections(overlay, overlay.common());
    REQUIRE(items[0].section.has_value());
    CHECK(items[0].section->scenery_item.empty());
    CHECK(items[0].section->references.size() == 1);
}
