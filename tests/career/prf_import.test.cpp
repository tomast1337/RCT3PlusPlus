// SPDX-License-Identifier: GPL-3.0-or-later
#include "career/prf_import.hpp"

#include "core/types.hpp"

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
        Bytes& put(const void* p, usize n)
        {
            const auto* b = static_cast<const std::byte*>(p);
            v.insert(v.end(), b, b + n);
            return *this;
        }
        Bytes& put_u8(u8 x)
        {
            return put(&x, 1);
        }
        Bytes& put_u16(u16 x)
        {
            return put(&x, 2);
        }
        Bytes& put_u32(u32 x)
        {
            return put(&x, 4);
        }
        Bytes& put_u64(u64 x)
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
            put_u16(static_cast<u16>(s.size()));
            for (const char c : s)
                put_u8(static_cast<u8>(c));
            return *this;
        }
        Bytes& blob(const Bytes& payload)
        {
            put_u32(static_cast<u32>(payload.v.size()));
            return cat(payload);
        }
        Bytes& text(const std::u16string& s)
        {
            Bytes p;
            p.put_u32(0xEFEFEFEFu); // Marks UTF-16 text
            for (const char16_t c : s)
                p.put_u16(c);
            return blob(p);
        }
    };

    struct Attr
    {
        std::string name, type;
        u32 size = 0;
        std::vector<Attr> children { };
    };

    void write_attr(Bytes& b, const Attr& a)
    {
        b.name(a.name).name(a.type).put_u32(a.size).put_u32(static_cast<u32>(a.children.size()));
        for (const auto& c : a.children)
            write_attr(b, c);
    }

    struct Entry
    {
        std::u16string last_save;
        u32 status = 0;
    };

    std::vector<std::byte> make_prf(const std::u16string& name, i32 pack, i32 current, const std::vector<Entry>& entries, std::string class_name = "ManagedPlayerProfile")
    {
        const std::vector<Attr> attrs = {
            { "AOPIndex", "int32", 4 },
            { "CurrentScenario", "int32", 4 },
            { "PlayerName", "string" },
            { "ScenarioData", "array", 0, { { "LastSaveName", "string" }, { "Status", "int32", 4 } } },
        };
        Bytes b;
        b.put_u32(1);
        b.name(class_name).put_u32(static_cast<u32>(attrs.size()));
        for (const auto& a : attrs)
            write_attr(b, a);
        Bytes list;
        list.put_u32(static_cast<u32>(entries.size()));
        for (const auto& e : entries)
            list.text(e.last_save).put_u32(e.status);
        b.put_u32(1).put_u32(0).put_u64(5);
        b.put_u32(static_cast<u32>(pack)).put_u32(static_cast<u32>(current)).text(name).blob(list);
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
