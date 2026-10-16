// SPDX-License-Identifier: GPL-3.0-or-later
#include "save/model/park.hpp"

#include "save/save_file.hpp"
#include "save/value.hpp"

#include <cmath>
#include <cstdint>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <unistd.h>
#include <vector>

using namespace rct3::save;
namespace fs = std::filesystem;

namespace
{

    // ---------------------------------------------------------------------------------------------------------
    // Synthetic save builder. Class and attribute names follow the format's identifiers; every value, text and
    // asset name below is invented.
    // ---------------------------------------------------------------------------------------------------------

    struct Bytes
    {
        std::vector<std::byte> v;
        Bytes& u8(std::uint8_t x)
        {
            v.push_back(static_cast<std::byte>(x));
            return *this;
        }
        Bytes& put(const void* p, std::size_t n)
        {
            const auto* b = static_cast<const std::byte*>(p);
            v.insert(v.end(), b, b + n);
            return *this;
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
        Bytes& f32(float x)
        {
            return put(&x, 4);
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
    };

    struct Attr
    {
        std::string name, type;
        std::uint32_t size = 0;
        std::vector<Attr> children;
    };

    void write_attr(Bytes& b, const Attr& a)
    {
        b.name(a.name).name(a.type).u32(a.size).u32(static_cast<std::uint32_t>(a.children.size()));
        for (const auto& c : a.children)
            write_attr(b, c);
    }

    struct Field
    {
        Attr attr;
        Bytes data; ///< Exactly the bytes the object stores for this attribute (with length prefix if variable)
    };

    Field fint(const std::string& n, std::int32_t v)
    {
        return { { n, "int32", 4, { } }, Bytes().u32(static_cast<std::uint32_t>(v)) };
    }
    Field ffloat(const std::string& n, float v)
    {
        return { { n, "float32", 4, { } }, Bytes().f32(v) };
    }
    Field fbool(const std::string& n, bool v)
    {
        return { { n, "bool", 1, { } }, Bytes().u8(v ? 1 : 0) };
    }
    Field fref(const std::string& n, std::uint64_t id)
    {
        return { { n, "reference", 8, { } }, Bytes().u64(id) };
    }
    Field fstr(const std::string& n, const std::string& text)
    {
        Bytes p;
        p.u32(0xEFEFEFEFu);
        for (const char c : text)
            p.u16(static_cast<std::uint16_t>(static_cast<unsigned char>(c)));
        return { { n, "string", 0, { } }, Bytes().blob(p) };
    }
    Field fgraph(const std::string& n, const std::vector<float>& values, std::uint32_t capacity = 64)
    {
        Bytes p;
        p.u32(0xFFFFFFFFu).u32(capacity).u32(static_cast<std::uint32_t>(values.size()));
        for (const float f : values)
            p.f32(f);
        return { { n, "graphedValue", 0, { } }, Bytes().blob(p) };
    }
    Field fintlist(const std::string& n, const std::vector<std::int32_t>& values)
    {
        Bytes p;
        p.u32(static_cast<std::uint32_t>(values.size()));
        for (const auto x : values)
            p.u32(static_cast<std::uint32_t>(x));
        return { { n, "list", 0, { { "n", "int32", 4, { } } } }, Bytes().blob(p) };
    }
    /// A list of struct elements, each with the given int32 members.
    Field fledger(const std::string& n, const std::vector<std::string>& members, const std::vector<std::vector<std::int32_t>>& rows)
    {
        Attr a { n, "list", 0, { } };
        for (const auto& m : members)
            a.children.push_back({ m, "int32", 4, { } });
        Bytes p;
        p.u32(static_cast<std::uint32_t>(rows.size()));
        for (const auto& r : rows)
        {
            for (const auto x : r)
                p.u32(static_cast<std::uint32_t>(x));
        }
        return { a, Bytes().blob(p) };
    }
    Field frefs(const std::string& n, const std::vector<std::uint64_t>& ids)
    {
        Bytes p;
        p.u32(static_cast<std::uint32_t>(ids.size()));
        for (const auto id : ids)
            p.u64(id);
        return { { n, "list", 0, { { "r", "reference", 8, { } } } }, Bytes().blob(p) };
    }

    struct Obj
    {
        std::string cls;
        std::uint64_t id;
        std::vector<Field> fields;
    };

    std::vector<std::byte> build(const std::vector<Obj>& objects)
    {
        Bytes b;
        b.u32(static_cast<std::uint32_t>(objects.size())); // one class per object, in order
        for (const auto& o : objects)
        {
            b.name(o.cls).u32(static_cast<std::uint32_t>(o.fields.size()));
            for (const auto& f : o.fields)
                write_attr(b, f.attr);
        }
        b.u32(static_cast<std::uint32_t>(objects.size()));
        std::uint32_t idx = 0;
        for (const auto& o : objects)
        {
            b.u32(idx++).u64(o.id);
            for (const auto& f : o.fields)
                b.cat(f.data);
        }
        return b.v;
    }

    struct Loaded
    {
        std::vector<std::byte> bytes;
        SaveFile file;
        ParkModel model;
    };

    std::unique_ptr<Loaded> load(const std::vector<Obj>& objects)
    {
        auto l = std::make_unique<Loaded>();
        l->bytes = build(objects);
        auto f = read_save(l->bytes);
        REQUIRE(f);
        l->file = std::move(*f);
        const ObjectIndex index(l->file);
        auto m = load_park(l->file, index);
        REQUIRE(m);
        l->model = std::move(*m);
        return l;
    }

    struct TempDir
    {
        fs::path path;
        TempDir()
        {
            path = fs::temp_directory_path() / ("rct3_park_test_" + std::to_string(::getpid()));
            fs::remove_all(path);
            fs::create_directories(path);
        }
        ~TempDir()
        {
            std::error_code ec;
            fs::remove_all(path, ec);
        }
        void touch(const std::string& rel) const
        {
            const auto p = path / rel;
            fs::create_directories(p.parent_path());
            std::ofstream(p).put('x');
        }
    };

} // namespace

TEST_CASE("park: a minimal park, economy and clock")
{
    const auto l = load(
        {
            { "Park", 10, { fstr("Name", "Test Park"), fint("Flags", 0x21), fint("OpeningTimeMinutes", 480), fint("ClosingTimeMinutes", 1200), ffloat("GuestGenerationRate", 0.75f), fgraph("ParkRating", { 650, 500, 400 }), fgraph("GuestsInPark", { 1, 2, 3, 4 }) } },
            { "Money", 11, { fint("Cash", 12345), fint("Loan", 100), fint("MaxLoan", 5000), ffloat("InterestRate", 7.5f), fgraph("ParkValue", { 10, 20 }), fledger("MonthlyExpendature", { "ShopSales", "Marketing", "PoolTickets" }, { { 1, 2, 3 }, { 4, 5, 6 } }) } },
            { "GameTime", 12, { ffloat("Time", 2880.0f + 720.0f + 270.0f + 10.0f), ffloat("ZeroTime", 5), ffloat("DayNightTime", 0.25f) } },
        });
    const auto& m = l->model;
    CHECK(m.has_park);
    CHECK(m.has_finance);
    CHECK(m.has_clock);
    CHECK_FALSE(m.has_awards);

    CHECK(m.park.name == "Test Park");
    CHECK(m.park.flags.has(ParkFlag::Open));
    CHECK(m.park.flags.has(ParkFlag::HarderRating));
    CHECK_FALSE(m.park.flags.has(ParkFlag::TreesLocked));
    CHECK(m.park.opening_minutes == 480);
    CHECK(m.park.guest_generation_rate == 0.75f);
    CHECK(m.park.rating().value() == 650.0f);
    CHECK(m.park.park_rating.capacity == 64);
    REQUIRE(m.park.guests_in_park.samples.size() == 4);
    CHECK(m.park.guests_in_park.samples[0] == 1.0f); // As in the file, newest first

    CHECK(m.finance.cash == 12345);
    CHECK(m.finance.interest_rate_percent == 7.5f);
    CHECK(m.finance.ledger_periods_loaded == 2);
    CHECK(m.finance.ledger[0].shop_sales == 1);
    CHECK(m.finance.ledger[1].marketing == 5);
    REQUIRE(m.finance.ledger[1].pool_tickets.has_value());
    CHECK(*m.finance.ledger[1].pool_tickets == 6);
    CHECK_FALSE(m.finance.ledger[1].billboards.has_value());
    CHECK(m.finance.ledger[2].shop_sales == 0); // absent period stays default

    const auto d = m.clock.date();
    CHECK(d.year == 1);
    CHECK(d.month == 2);
    CHECK(d.week == 3);
    CHECK(d.into_week == doctest::Approx(10.0f));
    REQUIRE(m.clock.day_night_phase.has_value());
    CHECK(*m.clock.day_night_phase == 0.25f);
    CHECK_FALSE(m.clock.day_night_mode.has_value());

    CHECK(validate(m).empty());
}

TEST_CASE("park: missing or mistyped attributes take documented defaults")
{
    const auto l = load(
        {
            { "Park", 1, { fstr("Name", "Only A Name"), fstr("Flags", "not a number"), fint("ParkRating", 5) } },
        });
    const auto& p = l->model.park;
    CHECK(p.name == "Only A Name");
    CHECK(p.flags.raw == 0);
    CHECK(p.guest_generation_rate == 1.0f);
    CHECK(p.ride_intensity_preference == 0.5f);
    CHECK(p.initial_happiness == 0.5f);
    CHECK(p.initial_hunger == 1.0f);
    CHECK(p.park_rating.empty()); // wrong type for a series
    CHECK_FALSE(p.rating().has_value());
    CHECK_FALSE(p.rating_breakdown_text.has_value());
    CHECK_FALSE(l->model.has_finance);
    // The absent singletons are reported by validation, not by failing the load.
    CHECK(validate(l->model).size() == 2);
}

TEST_CASE("park: a file with none of the objects loads as an empty model")
{
    const auto l = load({ { "Unrelated", 1, { fint("X", 1) } } });
    CHECK_FALSE(l->model.has_park);
    CHECK(l->model.content.entries.empty());
    CHECK(l->model.messages.ordered.empty());
}

TEST_CASE("park: sentinel values and null references mean none")
{
    const auto l = load(
        {
            { "Park", 1, { fref("ParkFenceEntry", 0) } },
            { "GameTime", 2, { ffloat("DayNightTime", -1.0f), fint("DayNightMode", 1), ffloat("DayNightTimeAsRendered", -1.0f) } },
        });
    CHECK_FALSE(l->model.park.fence_entry.has_value());
    CHECK_FALSE(l->model.clock.day_night_phase.has_value());
    CHECK_FALSE(l->model.clock.day_night_rendered_phase.has_value());
    REQUIRE(l->model.clock.day_night_mode.has_value());
    CHECK(*l->model.clock.day_night_mode == 1);
}

TEST_CASE("park: dangling references mean none, live ones are kept")
{
    const auto l = load(
        {
            { "Park", 1, { fref("ParkFenceEntry", 900) } },
            { "SIDDatabaseEntry", 50, { fstr("OVERLAYFILENAME", "a/b"), fstr("SYMBOLNAME", "s") } },
            { "SimpleResearch", 2, { fref("CurrentResearchItem", 50), fref("PreviousResearchItem", 901) } },
        });
    CHECK_FALSE(l->model.park.fence_entry.has_value());
    REQUIRE(l->model.research.current_item.has_value());
    CHECK(*l->model.research.current_item == 50);
    CHECK_FALSE(l->model.research.previous_item.has_value());
}

TEST_CASE("park: ring order")
{
    CHECK(ring_order(0, 0).empty());
    CHECK(ring_order(4, 0) == std::vector<std::size_t> { 0, 1, 2, 3 });
    CHECK(ring_order(4, 2) == std::vector<std::size_t> { 2, 3, 0, 1 });
    CHECK(ring_order(4, 3) == std::vector<std::size_t> { 3, 0, 1, 2 });
    CHECK(ring_order(4, 4) == std::vector<std::size_t> { 0, 1, 2, 3 }); // out of range counts as zero
    CHECK(ring_order(4, -3) == std::vector<std::size_t> { 0, 1, 2, 3 });
    CHECK(ring_order(3, std::numeric_limits<std::int64_t>::max()).size() == 3);
}

TEST_CASE("park: message log is ordered oldest first and skips dangling slots")
{
    const auto entry = [](std::uint64_t id, const char* text, float time) { return Obj { "MessageConsoleEntry", id, { fstr("Text", text), ffloat("Time", time) } }; };
    const auto l = load(
        {
            entry(21, "msg:a", 1.0f),
            entry(22, "msg:b", 2.0f),
            entry(23, "msg:c", 3.0f),
            { "ObjectMessageConsoleEntry", 24, { fstr("Text", "msg:d"), ffloat("Time", 4.0f), fref("Object", 21), fint("Panel", 3) } },
            { "MessageConsole", 20, { frefs("Messages", { 21, 22, 777, 24, 23 }), fint("NextMessage", 3) } },
        });
    const auto& log = l->model.messages;
    CHECK(log.ring_size == 5);
    CHECK(log.next_write == 3);
    REQUIRE(log.ordered.size() == 4); // slot 2 dangles
    // Slots from index 3 on, wrapping: 24, 23, 21, 22.
    CHECK(log.ordered[0].text_key == "msg:d");
    CHECK(log.ordered[1].text_key == "msg:c");
    CHECK(log.ordered[2].text_key == "msg:a");
    CHECK(log.ordered[3].text_key == "msg:b");
    REQUIRE(log.ordered[0].object.has_value());
    CHECK(*log.ordered[0].object == 21);
    REQUIRE(log.ordered[0].panel.has_value());
    CHECK(*log.ordered[0].panel == 3);
    CHECK_FALSE(log.ordered[1].object.has_value());
}

TEST_CASE("park: hostile counts and sizes")
{
    SUBCASE("a list claiming far more elements than there are bytes stays raw and loads as defaults")
    {
        Bytes p;
        p.u32(0xFFFFFFFFu).u32(1).u32(2);
        Field f { { "MonthlyExpendature", "list", 0, { { "ShopSales", "int32", 4, { } } } }, Bytes().blob(p) };
        const auto l = load({ { "Money", 1, { f, fint("Cash", 7) } } });
        CHECK(l->model.finance.cash == 7);
        CHECK(l->model.finance.ledger_periods_loaded == 0);
    }
    SUBCASE("more ledger periods than five are truncated")
    {
        std::vector<std::vector<std::int32_t>> rows(9, { 1 });
        const auto l = load({ { "Money", 1, { fledger("MonthlyExpendature", { "ShopSales" }, rows) } } });
        CHECK(l->model.finance.ledger_periods_loaded == kLedgerPeriods);
    }
    SUBCASE("a graph with more samples than capacity is kept and flagged by validation")
    {
        const auto l = load({ { "Park", 1, { fgraph("GuestsInPark", { 1, 2, 3, 4, 5 }, 2) } }, { "Money", 2, { } }, { "GameTime", 3, { } } });
        CHECK(l->model.park.guests_in_park.samples.size() == 5);
        CHECK(l->model.park.guests_in_park.capacity == 2);
        const auto issues = validate(l->model);
        REQUIRE(issues.size() == 1);
        CHECK(issues[0].find("capacity") != std::string::npos);
    }
    SUBCASE("a legacy graph without the marker word is read from its two-word header")
    {
        Bytes p;
        p.u32(16).u32(2).f32(5.0f).f32(6.0f);
        Field f { { "ParkRating", "graphedValue", 0, { } }, Bytes().blob(p) };
        const auto l = load({ { "Park", 1, { f } } });
        CHECK(l->model.park.park_rating.capacity == 16);
        REQUIRE(l->model.park.park_rating.samples.size() == 2);
        CHECK(l->model.park.park_rating.samples[1] == 6.0f);
    }
    SUBCASE("a legacy graph with a lying count is empty")
    {
        Bytes p;
        p.u32(16).u32(1000).f32(5.0f);
        Field f { { "ParkRating", "graphedValue", 0, { } }, Bytes().blob(p) };
        const auto l = load({ { "Park", 1, { f } } });
        CHECK(l->model.park.park_rating.empty());
    }
    SUBCASE("a next-write index far outside the ring is treated as zero")
    {
        const auto l = load({ { "MessageConsoleEntry", 5, { fstr("Text", "t"), ffloat("Time", 1) } }, { "MessageConsole", 6, { frefs("Messages", { 5 }), fint("NextMessage", -9999) } } });
        CHECK(l->model.messages.ordered.size() == 1);
    }
}

TEST_CASE("park: series push drops the oldest beyond capacity")
{
    Series s;
    s.capacity = 3;
    for (int i = 1; i <= 5; ++i)
        s.push(static_cast<float>(i));
    REQUIRE(s.samples.size() == 3);
    CHECK(s.samples.front() == 5.0f); // Newest first
    CHECK(s.samples.back() == 3.0f);
    CHECK(s.latest().value() == 5.0f);
    Series unbounded;
    for (int i = 0; i < 100; ++i)
        unbounded.push(1.0f);
    CHECK(unbounded.samples.size() == 100);
}

TEST_CASE("park: calendar split")
{
    const auto zero = GameClock::split(0.0f);
    CHECK(zero.year == 0);
    CHECK(zero.month == 0);
    CHECK(zero.week == 0);
    const auto edge = GameClock::split(89.0f);
    CHECK(edge.week == 0);
    CHECK(GameClock::split(90.0f).week == 1);
    CHECK(GameClock::split(359.0f).month == 0);
    CHECK(GameClock::split(360.0f).month == 1);
    CHECK(GameClock::split(2879.0f).month == 7);
    CHECK(GameClock::split(2880.0f).year == 1);
    for (const float bad : { -5.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
    {
        const auto d = GameClock::split(bad);
        CHECK(d.year == 0);
        CHECK(d.month == 0);
    }
    const auto huge = GameClock::split(3.0e38f);
    CHECK(huge.year > 0);
    CHECK(huge.month >= 0);
    CHECK(huge.month < 8);
}

TEST_CASE("park: awards, objectives, research")
{
    Bytes award_cats; // AwardsArray: list of elements, each an array of ids (a single variable child)
    award_cats.u32(3);
    const std::vector<std::vector<std::int32_t>> cats = { { }, { 4, 9 }, { } };
    for (const auto& c : cats)
    {
        Bytes inner;
        inner.u32(static_cast<std::uint32_t>(c.size()));
        for (const auto x : c)
            inner.u32(static_cast<std::uint32_t>(x));
        award_cats.blob(inner);
    }
    Field awards_array { { "AwardsArray", "list", 0, { { "AwardTypes", "array", 0, { { "AwardType", "int32", 4, { } } } } } }, Bytes().blob(award_cats) };
    const auto l = load(
        {
            { "Awards", 1, { awards_array, fint("AwardsAvailable", -1), fint("MonthsBeforeAwardsStart", 2), fintlist("SettingArray", { 100, -100, 0 }), fint("NumDangerousPoolEvents", 4) } },
            { "Objective", 2, { fint("Difficulty", 1), fint("ObjectiveId", 0), fint("Prize", 12), fbool("Success", false) } },
            { "ParkValueCondition", 3, { fint("Difficulty", 1), fint("ObjectiveId", 0), fint("ConditionId", 1), fbool("Completed", false), fint("Consistant", 3), fint("ConsistantFlags", 0x15), ffloat("StartTime", 10), ffloat("NextCheckTime", 500), fint("Value", 750000) } },
            { "MadeUpCondition", 4, { fint("Difficulty", 1), fint("ObjectiveId", 0), fint("ConditionId", 0), fbool("GT", true) } },
            { "ParkRatingCondition", 5, { fint("Difficulty", 2), fint("ObjectiveId", 0), fint("ConditionId", 0) } },
            { "ResearchCategoryInstance", 6, { fstr("CategoryName", "Cat"), ffloat("Budget", 2), fbool("IsAvailable", true), fbool("IsHidden", false) } },
            { "ResearchCategoryInstance", 7, { fstr("CategoryName", "Cat2"), fbool("IsInvented", true) } },
        });
    const auto& m = l->model;
    REQUIRE(m.has_awards);
    CHECK(m.awards.available_mask == 0xFFFFFFFFu);
    CHECK(m.awards.months_before_start == 2);
    REQUIRE(m.awards.earned.size() == 3);
    CHECK(m.awards.earned[1] == std::vector<std::int32_t> { 4, 9 });
    CHECK(m.awards.prize_money == std::vector<std::int32_t> { 100, -100, 0 });
    CHECK(m.awards.dangerous_pool_events == 4);
    CHECK_FALSE(m.awards.dangerous_coaster_events.has_value());

    REQUIRE(m.objectives.objectives.size() == 1);
    CHECK(m.objectives.objectives[0].prize_id == 12); // out-of-range ids stay representable
    CHECK_FALSE(m.objectives.objectives[0].prize_in_range());
    REQUIRE(m.objectives.conditions.size() == 3);
    const auto set = m.objectives.conditions_for(1, 0);
    REQUIRE(set.size() == 2);
    CHECK(set[0]->slot == 0); // slot order, regardless of file order
    CHECK(set[0]->kind == ConditionKind::Other);
    CHECK(set[0]->class_name == "MadeUpCondition");
    CHECK(set[0]->greater_than == true);
    CHECK(set[1]->kind == ConditionKind::ParkValue);
    CHECK(set[1]->value == 750000);
    CHECK(set[1]->months_to_hold == 3);
    CHECK(set[1]->has(ConditionFlag::TimerRunning));
    CHECK_FALSE(set[1]->has(ConditionFlag::Satisfied));
    CHECK(set[1]->has(ConditionFlag::HasDeadline));
    CHECK(set[1]->next_check_time == 500.0f);
    CHECK(m.objectives.conditions_for(2, 0).size() == 1);
    CHECK(m.objectives.conditions_for(0, 0).empty());

    REQUIRE(m.research.categories.size() == 2);
    CHECK(m.research.categories[0].budget == 2.0f);
    CHECK_FALSE(m.research.categories[0].invented.has_value());
    CHECK(m.research.categories[1].invented == true);
    CHECK_FALSE(m.research.has_simple_research);

    const auto issues = validate(m);
    bool prize = false;
    for (const auto& s : issues)
        prize = prize || s.find("prize") != std::string::npos;
    CHECK(prize);
}

TEST_CASE("park: validation reports broken invariants")
{
    const auto l = load(
        {
            { "Park", 1, { fgraph("ParkRating", { 1200 }), fint("OpeningTimeMinutes", 5000), fint("ClosingTimeMinutes", 1000) } },
            { "Money", 2, { fint("Loan", 90), fint("MaxLoan", 50) } },
            { "GameTime", 3, { ffloat("Time", std::numeric_limits<float>::quiet_NaN()) } },
        });
    const auto issues = validate(l->model);
    CHECK(issues.size() == 4);
}

TEST_CASE("park: required content, overlay stems and particle skipping")
{
    const auto l = load(
        {
            { "SIDDatabaseEntry", 1, { fbool("IsAvailable", true), fbool("IsHidden", false), fbool("IsInvented", true), fstr("OVERLAYFILENAME", "Style\\Invented\\Sample"), fstr("SYMBOLNAME", "piece1") } },
            { "AIDDatabaseEntry", 2, { fbool("IsAvailable", true), fstr("OVERLAYFILENAME", "./rides//Mock.common.ovl"), fstr("SYMBOLNAME", "ride9") } },
            { "WASDatabaseEntry", 3, { fstr("OVERLAYFILENAME", "wild/Zone"), fstr("SYMBOLNAME", "z"), fbool("ISUNLOCKED", true) } },
            { "SIDDatabaseEntry", 4, { fstr("OVERLAYFILENAME", "../escape/Out"), fstr("SYMBOLNAME", "bad") } },
            { "PPGDatabaseEntry", 5, { fstr("SymbolName", "person") } },
            { "ParticleDesc", 6, { fstr("Name", "p") } },
            { "ParticleEmitterDesc", 7, { fint("NumParticles", 3) } },
            { "ParticleSource", 8, { } },
        });
    const auto& c = l->model.content;
    REQUIRE(c.entries.size() == 3);
    CHECK(c.entries_without_overlay == 2); // symbol-only entry, and the stem that tries to leave the root
    CHECK(c.entries[0].kind == AssetKind::Scenery);
    CHECK(c.entries[0].overlay_stem == "Style/Invented/Sample");
    CHECK(c.entries[0].symbol == "piece1");
    CHECK(c.entries[0].invented);
    CHECK(c.entries[0].available);
    CHECK_FALSE(c.entries[0].hidden);
    CHECK(c.entries[1].kind == AssetKind::Attraction);
    CHECK(c.entries[1].overlay_stem == "rides/Mock");
    CHECK(c.entries[2].kind == AssetKind::AnimalSpecies);
    CHECK(c.entries[2].unlocked);
    CHECK_FALSE(c.entries[0].unlocked);
    CHECK(c.invented_count() == 1);
    CHECK(l->model.particle_objects == 3);
}

TEST_CASE("park: overlay stem normalisation")
{
    CHECK(normalise_overlay_stem("a/b/c") == "a/b/c");
    CHECK(normalise_overlay_stem("\\a\\\\b/c.OVL") == "a/b/c");
    CHECK(normalise_overlay_stem("a/b.common.ovl") == "a/b");
    CHECK(normalise_overlay_stem("a/b.unique.OVL") == "a/b");
    CHECK(normalise_overlay_stem("Keep.This") == "Keep.This");
    CHECK_FALSE(normalise_overlay_stem("").has_value());
    CHECK_FALSE(normalise_overlay_stem("///").has_value());
    CHECK_FALSE(normalise_overlay_stem("a/../b").has_value());
    CHECK_FALSE(normalise_overlay_stem("C:/x").has_value());
    CHECK_FALSE(normalise_overlay_stem(".ovl").has_value());
}

TEST_CASE("park: overlay resolution is case-insensitive per directory")
{
    TempDir dir;
    dir.touch("Styles/Alpha/Sheet.common.ovl");
    dir.touch("Styles/Alpha/Sheet.unique.ovl");
    dir.touch("Mixed/Only.COMMON.ovl");
    dir.touch("lower/thing.common.ovl");
    dir.touch("lower/thing.unique.ovl");

    OverlayResolver r(dir.path);
    SUBCASE("exact spelling")
    {
        const auto f = r.resolve("Styles/Alpha/Sheet");
        REQUIRE(f.has_value());
        CHECK(f->exact_case);
        CHECK(f->common.filename() == "Sheet.common.ovl");
        REQUIRE(f->unique.has_value());
        CHECK(f->unique->filename() == "Sheet.unique.ovl");
    }
    SUBCASE("other case and backslashes")
    {
        const auto f = r.resolve("STYLES\\alpha\\sheet");
        REQUIRE(f.has_value());
        CHECK_FALSE(f->exact_case);
        CHECK(f->common.parent_path().filename() == "Alpha"); // real spelling is returned
        CHECK(f->common.filename() == "Sheet.common.ovl");
    }
    SUBCASE("extension case differs")
    {
        const auto f = r.resolve("mixed/only");
        REQUIRE(f.has_value());
        CHECK_FALSE(f->exact_case);
        CHECK(f->common.filename() == "Only.COMMON.ovl");
        CHECK_FALSE(f->unique.has_value());
    }
    SUBCASE("missing pieces")
    {
        CHECK_FALSE(r.resolve("Styles/Alpha/Other").has_value());
        CHECK_FALSE(r.resolve("Nowhere/Sheet").has_value());
        CHECK_FALSE(r.resolve("../Styles/Alpha/Sheet").has_value());
        CHECK_FALSE(r.resolve("").has_value());
    }
    SUBCASE("one-shot helper returns the common half")
    {
        const auto p = resolve_overlay(dir.path, "LOWER/THING");
        REQUIRE(p.has_value());
        CHECK(p->filename() == "thing.common.ovl");
        CHECK_FALSE(resolve_overlay(dir.path / "absent", "lower/thing").has_value());
    }
}

TEST_CASE("park: content check against an install")
{
    TempDir dir;
    dir.touch("Pack/One.common.ovl");
    dir.touch("Pack/One.unique.ovl");
    dir.touch("pack/two.common.ovl");

    RequiredContent c;
    const auto add = [&](const char* stem) {
        AssetRequirement r;
        r.overlay_stem = stem;
        c.entries.push_back(r);
    };
    add("Pack/One");
    add("Pack/One");
    add("PACK/Two");
    add("Pack/Three");
    add("Pack/Three");
    const auto rep = check_content(c, dir.path);
    CHECK(rep.entries == 5);
    CHECK(rep.resolved == 3);
    CHECK(rep.exact_case == 2);
    CHECK(rep.with_unique == 2);
    CHECK(rep.missing == 2);
    REQUIRE(rep.missing_stems.size() == 1);
    CHECK(rep.missing_stems[0] == "Pack/Three");
}
