// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/ratings.hpp"

#include <algorithm>
#include <doctest/doctest.h>

using namespace rct3::simulation;

namespace
{
    // An invented model: speed thrills, sideways force shakes and sickens; capped at 30 m/s.
    constexpr std::string_view kModel = R"({
        "features": { "max_speed": 30, "side_force": 10 },
        "intensity_penalty": { "bounds": [5, 6], "keep": 0.5 },
        "families": {
            "fast": { "types": ["Zoom:trr"], "excitement": { "one": 1, "max_speed": 0.2 }, "intensity": { "side_force": 2 }, "nausea": { "side_force": 1 } },
            "default": { "excitement": { "one": 2 } }
        }
    })";

    RatingModel model()
    {
        auto parsed = parse_rating_model(kModel);
        REQUIRE(parsed);
        return *parsed;
    }
} // namespace

TEST_CASE("a ride's ratings are its family's weights over its test run, each feature capped")
{
    const RatingModel m = model();
    rct3::save::TestResults t;
    t.max_speed = 20;
    t.max_lateral = 2 * 9.81f;
    const Ratings r = rate(m, "zoom:TRR", t);
    CHECK(r.excitement == doctest::Approx(5));
    CHECK(r.intensity == doctest::Approx(4));
    CHECK(r.nausea == doctest::Approx(2));
    t.max_speed = 50; // Past the cap
    CHECK(rate(m, "Zoom:trr", t).excitement == doctest::Approx(7));

    SUBCASE("a type in no family is rated by the default one")
    {
        CHECK(rate(m, "Other:trr", t).excitement == doctest::Approx(2));
    }
}

TEST_CASE("very intense rides lose excitement at each bound passed")
{
    const RatingModel m = model();
    rct3::save::TestResults t;
    t.max_speed = 20;
    t.max_lateral = 2.5f * 9.81f; // Intensity 5: one bound
    CHECK(rate(m, "Zoom:trr", t).excitement == doctest::Approx(2.5));
    t.max_lateral = 4 * 9.81f; // Intensity 8: both
    CHECK(rate(m, "Zoom:trr", t).excitement == doctest::Approx(1.25));
}

TEST_CASE("a model with an unknown feature or no families is refused")
{
    CHECK_FALSE(parse_rating_model("[]"));
    CHECK_FALSE(parse_rating_model(R"({ "features": {} })"));
    CHECK_FALSE(parse_rating_model(R"({ "features": { "wings": 1 }, "families": {} })"));
    CHECK_FALSE(parse_rating_model(R"({ "features": {}, "families": { "a": { "nausea": { "wings": 1 } } } })"));
}

TEST_CASE("the built-in model has a default family")
{
    const RatingModel& m = builtin_rating_model();
    CHECK(std::ranges::any_of(m.families, [](const RatingModel::Family& f) { return f.name == "default"; }));
}
