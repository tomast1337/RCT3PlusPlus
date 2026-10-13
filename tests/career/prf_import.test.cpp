// SPDX-License-Identifier: GPL-3.0-or-later
#include "career/prf_import.hpp"

#include <cstdint>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace rct3::career;

namespace
{

    // Synthetic career files. Class and attribute names follow the format's identifiers; every value is
    // invented.

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

    struct Entry
    {
        std::u16string last_save;
        std::uint32_t status = 0;
    };

    std::vector<std::byte> make_prf(const std::u16string& name, std::int32_t pack, std::int32_t current, const std::vector<Entry>& entries, std::string class_name = "ManagedPlayerProfile")
    {
        const std::vector<Attr> attrs = {
            { "AOPIndex", "int32", 4 },
            { "CurrentScenario", "int32", 4 },
            { "PlayerName", "string" },
            { "ScenarioData", "array", 0, { { "LastSaveName", "string" }, { "Status", "int32", 4 } } },
        };
        Bytes b;
        b.u32(1);
        b.name(class_name).u32(static_cast<std::uint32_t>(attrs.size()));
        for (const auto& a : attrs)
            write_attr(b, a);
        Bytes list;
        list.u32(static_cast<std::uint32_t>(entries.size()));
        for (const auto& e : entries)
            list.text(e.last_save).u32(e.status);
        b.u32(1).u32(0).u64(5);
        b.u32(static_cast<std::uint32_t>(pack)).u32(static_cast<std::uint32_t>(current)).text(name).blob(list);
        return b.v;
    }

} // namespace

TEST_CASE("a career of the original game reads with its name, game and progress")
{
    const auto prf = read_prf(make_prf(u"Tess", 0, 1, { { u"", 5 }, { u"Fair.dat", 3 }, { u"", 1 } }));
    REQUIRE(prf);
    CHECK(prf->name == "Tess");
    CHECK(prf->pack == 1);
    CHECK(prf->current == 1);
    REQUIRE(prf->scenarios.size() == 3);
    CHECK(prf->scenarios[0].status == 5);
    CHECK(prf->scenarios[1].last_save == "Fair.dat");
}

TEST_CASE("brought over, scenarios are named by the game's list and saves found in their folders")
{
    const auto prf = read_prf(make_prf(u"Tess", 1, 2, { { u"", 5 }, { u"Fair.dat", 4 }, { u"", 2 }, { u"", 9 }, { u"", 3 } }));
    REQUIRE(prf);
    const std::vector<std::string> ids { "Cove", "Oasis", "Reef", "Lagoon" };
    const Career c = career_from_prf(*prf, ids);
    CHECK(c.pack == 2);
    REQUIRE(c.scenarios.size() == 4); // The fifth is past the list's end
    CHECK(c.scenarios[0].id == "Cove");
    CHECK(c.scenarios[0].status == ScenarioStatus::Tycoon);
    CHECK(c.scenarios[1].status == ScenarioStatus::Entrepreneur);
    CHECK(c.scenarios[1].last_save == "Scenario02/Fair.dat");
    CHECK(c.scenarios[2].status == ScenarioStatus::Open);
    CHECK(c.scenarios[3].status == ScenarioStatus::Locked); // An unknown status is locked; the last waits for tycoon
    CHECK(c.current == 2);
}

TEST_CASE("files without a career, or of an unknown game, say so")
{
    CHECK(read_prf(std::vector<std::byte>(3)).error() == PrfError::NotASave);
    CHECK(read_prf(make_prf(u"X", -1, 0, { }, "Other")).error() == PrfError::NoCareer);
    CHECK(read_prf(make_prf(u"X", 5, 0, { })).error() == PrfError::UnknownPack);
    CHECK(prf_scenario_folder(11) == "Scenario12");
}
