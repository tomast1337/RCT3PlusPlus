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
        w[4] = 2;
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
        pool.add("CurveL_WaterSpline:spl");
        pool.add("CurveR_WaterSpline:spl");
        const auto bytes = record(version, pool);
        const auto overlay = make_record_overlay(version, pool, "Curve:tks", bytes, { { 4, "Curve:sid" }, { 0x20, "CurveL:spl" }, { 0x24, "CurveR:spl" }, { 0x28, "CurveL:spl" }, { 0x2C, "CurveR:spl" }, { 0x30, "Extra:spl" }, { 0xD8, "CurveL_WaterSpline:spl" }, { 0xDC, "CurveR_WaterSpline:spl" } });
        const auto items = read_track_sections(overlay, overlay.common());
        REQUIRE(items.size() == 1);
        CHECK(items[0].name == "Curve");
        REQUIRE(items[0].section.has_value());
        const auto& s = *items[0].section;
        CHECK(s.name == "Curve");
        CHECK(s.scenery_item == "Curve");
        CHECK(s.join_a == 3);
        CHECK(s.join_b == 6);
        CHECK(s.role == 2);
        CHECK(s.flags[0] == 0x400004);
        CHECK(s.flags[1] == 0x400002);
        CHECK(s.splines[0] == "CurveL");
        CHECK(s.splines[1] == "CurveR");
        CHECK(s.splines[3] == "CurveR");
        CHECK(s.extra_splines[0] == "Extra");
        CHECK(s.extra_splines[1].empty());
        CHECK(s.water_splines[0] == "CurveL_WaterSpline");
        CHECK(s.water_splines[1] == "CurveR_WaterSpline");
        CHECK(s.scale[1] == 2.0f);
        CHECK(s.references.size() == 8);
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

TEST_CASE("later track sections give their join names, widths, group, tags and side")
{
    for (const std::uint32_t version : { 4u, 5u })
    {
        CAPTURE(version);
        Pool pool;
        const auto bottom = pool.add("bottom");
        const auto top = pool.add("top");
        const auto group = pool.add("station");
        const auto offer = pool.add("Shelf");
        const auto forbid = pool.add("Ledge");
        pool.add("Base:tks");
        const std::uint32_t base = pool.base();
        std::vector<std::uint32_t> w(kSize[version] / 4 + 3, 0);
        const auto list = static_cast<std::uint32_t>(base + kSize[version]); // three string pointers past the record
        w[kSize[version] / 4] = offer;
        w[kSize[version] / 4 + 1] = forbid;
        w[kSize[version] / 4 + 2] = offer;
        w[0x50 / 4] = bottom;
        w[0x6C / 4] = top;
        w[0xE4 / 4] = version == 4 ? 2 : 3;
        w[0x104 / 4] = group;
        w[0x114 / 4] = 1;
        w[0x13C / 4] = 1; // the start offers Shelf
        w[0x140 / 4] = list;
        w[0x154 / 4] = 1; // the end needs Ledge
        w[0x158 / 4] = list + 4;
        w[0x164 / 4] = 1; // the end forbids Shelf
        w[0x168 / 4] = list + 8;
        if (version == 5)
            w[0x16C / 4] = 2;
        std::vector<std::byte> bytes;
        for (auto v : w)
            append_u32(bytes, v);
        const auto overlay = make_record_overlay(version, pool, "Base:tks", bytes);
        const auto items = read_track_sections(overlay, overlay.common());
        REQUIRE(items[0].section.has_value());
        const auto& s = *items[0].section;
        CHECK(s.start_join == "bottom");
        CHECK(s.end_join == "top");
        CHECK(s.widths == std::array<std::uint32_t, 2> { 1, 0 });
        CHECK(s.group == "station");
        CHECK(s.tags[0].offers == std::vector<std::string_view> { "Shelf" });
        CHECK(s.tags[0].needs.empty());
        CHECK(s.tags[1].needs == std::vector<std::string_view> { "Ledge" });
        CHECK(s.tags[1].forbids == std::vector<std::string_view> { "Shelf" });
        CHECK(s.side == (version == 5 ? 2u : 0u));
    }
}
