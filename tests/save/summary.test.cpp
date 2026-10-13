// SPDX-License-Identifier: GPL-3.0-or-later
#include "save/summary.hpp"

#include <cstdint>
#include <doctest/doctest.h>
#include <optional>
#include <string>
#include <vector>

using namespace rct3::save;

namespace
{

    // Synthetic saves. Class and attribute names follow the format's identifiers; every value is invented.

    struct Bytes
    {
        std::vector<std::byte> v;
        Bytes& put(const void* p, std::size_t n)
        {
            const auto* b = static_cast<const std::byte*>(p);
            v.insert(v.end(), b, b + n);
            return *this;
        }
        Bytes& u8(std::uint8_t x)
        {
            return put(&x, 1);
        }
        Bytes& u16(std::uint16_t x)
        {
            return put(&x, 2);
        }
        Bytes& u32(std::uint32_t x)
        {
            return put(&x, 4);
        }
        Bytes& u64(std::uint64_t x)
        {
            return put(&x, 8);
        }
        Bytes& cat(const Bytes& o)
        {
            v.insert(v.end(), o.v.begin(), o.v.end());
            return *this;
        }
        Bytes& name(const std::string& s)
        {
            u16(static_cast<std::uint16_t>(s.size()));
            for (const char c : s)
                u8(static_cast<std::uint8_t>(c));
            return *this;
        }
        Bytes& blob(const Bytes& payload)
        {
            u32(static_cast<std::uint32_t>(payload.v.size()));
            return cat(payload);
        }
        Bytes& text(const std::u16string& s)
        {
            Bytes p;
            p.u32(0xEFEFEFEFu); // Marks UTF-16 text
            for (const char16_t c : s)
                p.u16(c);
            return blob(p);
        }
    };

    struct Attr
    {
        std::string name, type;
        std::uint32_t size = 0;
        std::vector<Attr> children { };
    };

    void write_attr(Bytes& b, const Attr& a)
    {
        b.name(a.name).name(a.type).u32(a.size).u32(static_cast<std::uint32_t>(a.children.size()));
        for (const auto& c : a.children)
            write_attr(b, c);
    }

    const std::vector<Attr> kSummaryAttrs = {
        { "DatParkName", "string" }, { "DatDay", "int32", 4 }, { "DatMonth", "int32", 4 }, { "DatYear", "int32", 4 }, { "DatMinutesSinceMidnight", "int32", 4 }, { "DatGuestCountInt", "int32", 4 }, { "DatType", "int32", 4 }, { "DatPreview", "array", 0, { { "B", "uint8", 1 }, { "G", "uint8", 1 }, { "R", "uint8", 1 } } }, { "AddOnPacksNeeded", "array", 0, { { "Name", "string" }, { "VersionMask", "uint32", 4 } } },
    };

    struct Summary
    {
        std::u16string name = u"Tea Gardens";
        std::uint32_t day = 3, month = 2, year = 4, minutes = 600, guests = 75, kind = 0;
        std::uint32_t pixels = kPreviewWidth * kPreviewHeight;
        std::vector<std::u16string> packs;
    };

    Bytes summary_payload(const Summary& s)
    {
        Bytes preview;
        preview.u32(s.pixels);
        for (std::uint32_t i = 0; i < s.pixels; ++i)
            preview.u8(static_cast<std::uint8_t>(i)).u8(20).u8(30); // B, G, R
        Bytes packs;
        packs.u32(static_cast<std::uint32_t>(s.packs.size()));
        for (const auto& p : s.packs)
            packs.text(p).u32(1);
        Bytes b;
        b.text(s.name).u32(s.day).u32(s.month).u32(s.year).u32(s.minutes).u32(s.guests).u32(s.kind).blob(preview).blob(packs);
        return b;
    }

    /// A save with the summaries given (ids 10, 11, ...) and, when `current` is set, the object naming the
    /// save's own.
    std::vector<std::byte> make_save(const std::vector<Summary>& summaries, std::optional<std::uint64_t> current)
    {
        Bytes b;
        b.u32(3);
        b.name("Other").u32(1);
        write_attr(b, { "Number", "int32", 4 });
        b.name("GUI2_DatDesc").u32(static_cast<std::uint32_t>(kSummaryAttrs.size()));
        for (const auto& a : kSummaryAttrs)
            write_attr(b, a);
        b.name("GUI2_CurrentDatDesc").u32(1);
        write_attr(b, { "CurrDatDesc", "reference", 8 });

        b.u32(static_cast<std::uint32_t>(summaries.size() + 1 + (current ? 1 : 0)));
        b.u32(0).u64(1).u32(7);
        std::uint64_t id = 10;
        for (const auto& s : summaries)
            b.u32(1).u64(id++).cat(summary_payload(s));
        if (current)
            b.u32(2).u64(99).u64(*current);
        return b.v;
    }

} // namespace

TEST_CASE("the summary gives the park's name, date, guests, picture and packs")
{
    Summary s;
    s.packs = { u"Pack One" };
    const auto bytes = make_save({ s }, std::nullopt);
    const auto read = read_summary(bytes);
    REQUIRE(read);
    CHECK(read->park_name == "Tea Gardens");
    CHECK(read->day == 3);
    CHECK(read->month == 2);
    CHECK(read->year == 4);
    CHECK(read->minutes == 600);
    CHECK(read->guests == 75);
    CHECK(read->kind == 0);
    REQUIRE(read->preview.size() == std::size_t { kPreviewWidth } * kPreviewHeight * 4);
    // Stored blue, green, red; given red, green, blue, opaque.
    CHECK(read->preview[4 * 5 + 0] == 30);
    CHECK(read->preview[4 * 5 + 1] == 20);
    CHECK(read->preview[4 * 5 + 2] == 5);
    CHECK(read->preview[4 * 5 + 3] == 255);
    CHECK(read->packs == std::vector<std::string> { "Pack One" });
}

TEST_CASE("the summary the save names wins, else the first describing a park")
{
    Summary other;
    other.kind = 1;
    other.name = u"Not a park";
    Summary park;
    park.name = u"Lakeside";
    Summary third;
    third.name = u"Named";
    third.kind = 1;

    const auto first_park = read_summary(make_save({ other, park, third }, std::nullopt));
    REQUIRE(first_park);
    CHECK(first_park->park_name == "Lakeside");

    const auto named = read_summary(make_save({ other, park, third }, 12));
    REQUIRE(named);
    CHECK(named->park_name == "Named");

    // A name that leads nowhere falls back to the first park.
    const auto dangling = read_summary(make_save({ other, park }, 50));
    REQUIRE(dangling);
    CHECK(dangling->park_name == "Lakeside");
}

TEST_CASE("a picture of the wrong size is left out")
{
    Summary s;
    s.pixels = 10;
    const auto read = read_summary(make_save({ s }, std::nullopt));
    REQUIRE(read);
    CHECK(read->preview.empty());
}

TEST_CASE("files that are not saves, or have no summary, say so")
{
    CHECK(read_summary(std::vector<std::byte>(3)).error() == SummaryError::NotASave);
    CHECK(read_summary(make_save({ }, std::nullopt)).error() == SummaryError::NoSummary);
    Summary only_other;
    only_other.kind = 1;
    CHECK(read_summary(make_save({ only_other }, std::nullopt)).error() == SummaryError::NoSummary);
}
