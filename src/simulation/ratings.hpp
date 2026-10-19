// SPDX-License-Identifier: GPL-3.0-or-later
//
// How exciting, intense and sickening a ride is, from what a test run found: our own model, not the original
// game's. The ratings are approximations: they land near what saved parks hold (about one rating point off on
// average when fitted), never exactly on it. They are on the same scale as a park keeps them, about 0 to 10 and
// more for wild rides.
//
// Each rating is a sum of weighted features of the test run (speeds, times, forces, drops...), each capped so a
// very long or wild ride stays in reach; then very intense rides lose excitement. Ride types come in families,
// each with its own weights. All of it is data: assets/simulation/ride_ratings.json, fitted to saved parks by
// tools/ratings-fit/fit.py, and a file of the same shape in the user's config folder replaces it.
#pragma once

#include "save/model/rides.hpp"
#include "simulation/coaster.hpp"

#include <array>
#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::simulation
{

    struct Ratings
    {
        float excitement = 0;
        float intensity = 0;
        float nausea = 0;
    };

    /// What the model reads off a test run, in the order of `kRatingFeatures`.
    inline constexpr std::array<std::string_view, 13> kRatingFeatures { "one", "max_speed", "average_speed", "ride_time", "ride_length", "up_force", "down_force", "side_force", "air_time", "drops", "highest_drop", "inversions", "lift_hills" };
    using RatingFeatures = std::array<float, kRatingFeatures.size()>;

    struct RatingModel
    {
        struct Family
        {
            std::string name;
            std::vector<std::string> types;            ///< Ride type symbols, as "Name:trr"; compared ignoring case
            std::array<RatingFeatures, 3> weights { }; ///< Excitement, intensity, nausea
        };
        RatingFeatures caps { };
        std::vector<float> penalty_bounds; ///< Intensities at each of which excitement is cut
        float penalty_keep = 1;            ///< Share of excitement kept at each bound passed
        std::vector<Family> families;      ///< The one named "default" rates types in no family
    };

    /// The model in `json` text. An error names what is wrong: not JSON, a missing part, an unknown feature.
    [[nodiscard]] std::expected<RatingModel, std::string> parse_rating_model(std::string_view json);

    /// The model built into the program.
    [[nodiscard]] const RatingModel& builtin_rating_model();

    /// The features of a test run, before caps. Forces are in metres per second squared.
    [[nodiscard]] RatingFeatures rating_features(const save::TestResults& test);

    /// The ratings a test run of a ride of type `type` earns under `model`.
    [[nodiscard]] Ratings rate(const RatingModel& model, std::string_view type, const save::TestResults& test);

    /// A test run of `coaster` from a standing start, as far as the model knows: speeds, time, length and the
    /// drops it goes down. Forces, air time and turns are left at nothing. Nothing if the train does not come
    /// round within `most` seconds.
    [[nodiscard]] std::optional<save::TestResults> test_lap(Coaster coaster, float most = 1200);

} // namespace rct3::simulation
