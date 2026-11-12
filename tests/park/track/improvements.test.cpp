// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track/improvements.hpp"

#include <doctest/doctest.h>

using namespace rct3::park::track;

namespace
{
    PieceShape piece(std::string name, PieceUse use = PieceUse::Special)
    {
        PieceShape s;
        s.record.name = std::move(name);
        s.use = use;
        return s;
    }

    const std::vector<Improvement> kTable {
        { .id = "loops", .name = "Loops", .cost = 200, .pieces = { "loop" } },
        { .id = "launches", .name = "Launches", .cost = 300, .pieces = { "launchsection" } },
    };
} // namespace

TEST_CASE("a table of improvements is read with each one's id, name, cost and piece words")
{
    const auto table = parse_improvements(R"({ "about": "x", "improvements": [
        { "id": "loops", "name": "Key_Loops", "cost": 20000, "pieces": ["Loop"] },
        { "id": "rolls", "name": "Key_Rolls", "cost": 0, "pieces": ["roll", "twist"] } ] })");
    REQUIRE(table);
    REQUIRE(table->size() == 2);
    CHECK((*table)[0].id == "loops");
    CHECK((*table)[0].name == "Key_Loops");
    CHECK((*table)[0].cost == 20000);
    CHECK((*table)[0].pieces == std::vector<std::string> { "loop" }); // In lower case
    CHECK((*table)[1].pieces.size() == 2);
}

TEST_CASE("a bad table of improvements is refused, naming what is wrong")
{
    CHECK_FALSE(parse_improvements("[]"));
    CHECK_FALSE(parse_improvements(R"({ "improvements": {} })"));
    SUBCASE("an entry without an id")
    {
        const auto t = parse_improvements(R"({ "improvements": [ { "name": "A", "cost": 1, "pieces": ["a"] } ] })");
        REQUIRE_FALSE(t);
        CHECK(t.error().contains("improvement 1"));
    }
    SUBCASE("an id given twice")
    {
        const auto t = parse_improvements(R"({ "improvements": [ { "id": "a", "name": "A", "cost": 1, "pieces": ["a"] }, { "id": "a", "name": "A", "cost": 1, "pieces": ["a"] } ] })");
        REQUIRE_FALSE(t);
        CHECK(t.error().contains("twice"));
    }
    SUBCASE("a cost below nought or not whole")
    {
        CHECK_FALSE(parse_improvements(R"({ "improvements": [ { "id": "a", "name": "A", "cost": -1, "pieces": ["a"] } ] })"));
        CHECK_FALSE(parse_improvements(R"({ "improvements": [ { "id": "a", "name": "A", "cost": 1.5, "pieces": ["a"] } ] })"));
    }
    SUBCASE("no piece words, or one not text")
    {
        CHECK_FALSE(parse_improvements(R"({ "improvements": [ { "id": "a", "name": "A", "cost": 1, "pieces": [] } ] })"));
        CHECK_FALSE(parse_improvements(R"({ "improvements": [ { "id": "a", "name": "A", "cost": 1, "pieces": [3] } ] })"));
    }
}

TEST_CASE("the built-in table reads")
{
    CHECK_FALSE(builtin_improvements().empty());
}

TEST_CASE("a piece of a locked improvement's family waits for it; invented, it may be built")
{
    const std::vector<std::string> loops_locked { "loops" };
    const auto locking = locking_improvement(kTable, loops_locked, piece("LoopLeft"));
    REQUIRE(locking);
    CHECK(locking->id == "loops");
    CHECK(locking_improvement(kTable, loops_locked, piece("InvHalfLoop"))); // Any case, anywhere in the name
    CHECK_FALSE(locking_improvement(kTable, loops_locked, piece("LaunchSection")));
    CHECK_FALSE(locking_improvement(kTable, { }, piece("LoopLeft")));
    SUBCASE("stations and pieces picked by shape never wait")
    {
        CHECK_FALSE(locking_improvement(kTable, loops_locked, piece("LoopStation", PieceUse::Station)));
        CHECK_FALSE(locking_improvement(kTable, loops_locked, piece("Loopish", PieceUse::Plain)));
    }
}
