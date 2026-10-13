// SPDX-License-Identifier: GPL-3.0-or-later
#include "career/career.hpp"

#include <doctest/doctest.h>

using namespace rct3::career;

namespace
{
    std::vector<std::string> ids(std::size_t n, std::string_view prefix = "S")
    {
        std::vector<std::string> out;
        for (std::size_t i = 0; i < n; ++i)
            out.push_back(std::string(prefix) + std::to_string(i));
        return out;
    }

    std::size_t open_count(const Career& c)
    {
        std::size_t n = 0;
        for (const auto& s : c.scenarios)
            n += is_open(s.status) ? 1 : 0;
        return n;
    }
} // namespace

TEST_CASE("a new career opens six scenarios of the original game, three of an expansion")
{
    CHECK(open_count(new_career("A", 0, ids(18))) == 6);
    CHECK(open_count(new_career("B", 1, ids(9))) == 3);
    CHECK(open_count(new_career("C", 2, ids(4))) == 2); // Never the last two
    CHECK(new_career("D", 0, ids(3)).scenarios[2].id == "S2");
}

TEST_CASE("each completed scenario opens one more, short of the last two")
{
    Career c = new_career("A", 1, ids(9));
    complete(c, 0, ScenarioStatus::Apprentice, 1);
    CHECK(open_count(c) == 4);
    complete(c, 1, ScenarioStatus::Tycoon, 1);
    for (std::size_t i = 2; i < 6; ++i)
        complete(c, i, ScenarioStatus::Apprentice, 1);
    CHECK(open_count(c) == 7);
    complete(c, 6, ScenarioStatus::Apprentice, 1);
    CHECK(open_count(c) == 7); // The second to last waits for entrepreneur on all seven

    for (std::size_t i = 0; i < 7; ++i)
        complete(c, i, ScenarioStatus::Entrepreneur, 1);
    CHECK(open_count(c) == 8);
    CHECK(c.scenarios[8].status == ScenarioStatus::Locked);
    for (std::size_t i = 0; i < 8; ++i)
        complete(c, i, ScenarioStatus::Tycoon, 1);
    CHECK(open_count(c) == 9);
}

TEST_CASE("completing never lowers a level, and dates each level first reached")
{
    Career c = new_career("A", 0, ids(18));
    complete(c, 2, ScenarioStatus::Entrepreneur, 100);
    CHECK(c.scenarios[2].reached == std::array<std::int64_t, 3> { 100, 100, 0 });
    complete(c, 2, ScenarioStatus::Tycoon, 200);
    complete(c, 2, ScenarioStatus::Apprentice, 300);
    CHECK(c.scenarios[2].status == ScenarioStatus::Tycoon);
    CHECK(c.scenarios[2].reached == std::array<std::int64_t, 3> { 100, 100, 200 });
    complete(c, 40, ScenarioStatus::Tycoon, 1); // Out of the list: nothing
}

TEST_CASE("the score counts levels, out of three a scenario")
{
    Career c = new_career("A", 1, ids(4));
    c.scenarios[0].status = ScenarioStatus::Apprentice;
    c.scenarios[1].status = ScenarioStatus::Entrepreneur;
    c.scenarios[2].status = ScenarioStatus::Tycoon;
    const CareerScore s = score(c);
    CHECK(s.completed == 3);
    CHECK(s.entrepreneur == 2);
    CHECK(s.tycoon == 1);
    CHECK(s.percent == doctest::Approx(50)); // 6 of 12
    CHECK(score(Career { }).percent == doctest::Approx(0));
}

TEST_CASE("fitting to a changed list keeps progress by id, and keeps what is no longer listed")
{
    Career c = new_career("A", 1, std::vector<std::string> { "Hill", "Lake", "Mine" });
    c.scenarios[1].status = ScenarioStatus::Tycoon;
    c.current = 2;
    fit(c, std::vector<std::string> { "Mine", "New", "Lake" });
    REQUIRE(c.scenarios.size() == 3);
    CHECK(c.scenarios[2].id == "Lake");
    CHECK(c.scenarios[2].status == ScenarioStatus::Tycoon);
    CHECK(c.current == 0); // Still on Mine
    REQUIRE(c.unlisted.size() == 1);
    CHECK(c.unlisted[0].id == "Hill");

    fit(c, std::vector<std::string> { "Hill", "Lake" }); // Back again
    CHECK(c.unlisted.size() == 2);
    CHECK(c.scenarios[0].id == "Hill");
    CHECK(c.current == 0);
}
