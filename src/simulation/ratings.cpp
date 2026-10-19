// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/ratings.hpp"

#include "assets/embedded_files.hpp"
#include "config/json_text.hpp"
#include "logging/log.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <limits>
#include <span>

namespace rct3::simulation
{

    namespace
    {
        constexpr float kGravity = 9.81f;
        constexpr float kSmallestDrop = 1; ///< Metres down before a descent counts as a drop
        constexpr std::string_view kModelPath = "simulation/ride_ratings.json";
        constexpr std::string_view kFallbackFamily = "default";
        constexpr std::array<std::string_view, 3> kRatingNames { "excitement", "intensity", "nausea" };

        std::optional<std::size_t> feature_index(std::string_view name)
        {
            const auto at = std::ranges::find(kRatingFeatures, name);
            return at == kRatingFeatures.end() ? std::nullopt : std::optional(static_cast<std::size_t>(at - kRatingFeatures.begin()));
        }

        /// The features named in `object`, as numbers; an error for a name the model does not know.
        std::expected<RatingFeatures, std::string> read_features(const config::Json& object, std::string_view what)
        {
            RatingFeatures out { };
            if (!object.is_object())
                return std::unexpected(std::format("{} is not an object", what));
            for (const auto& [name, value] : object.items())
            {
                const auto at = feature_index(name);
                if (!at)
                    return std::unexpected(std::format("{}: no feature {}", what, name));
                if (!value.is_number())
                    return std::unexpected(std::format("{}: {} is not a number", what, name));
                out[*at] = value.get<float>();
            }
            return out;
        }

        bool same_ignoring_case(std::string_view a, std::string_view b)
        {
            return std::ranges::equal(a, b, [](char x, char y) { return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); });
        }
    } // namespace

    std::expected<RatingModel, std::string> parse_rating_model(std::string_view json)
    {
        const auto root = config::parse_json_object(json, nullptr);
        if (!root)
            return std::unexpected("not a JSON object");
        RatingModel model;
        if (!root->contains("features") || !root->contains("families"))
            return std::unexpected("needs features and families");
        // Every feature has a cap; one left out reads as no cap.
        model.caps.fill(std::numeric_limits<float>::max());
        const auto caps = read_features((*root)["features"], "features");
        if (!caps)
            return std::unexpected(caps.error());
        for (const auto& [name, value] : (*root)["features"].items())
            model.caps[*feature_index(name)] = (*caps)[*feature_index(name)];
        if (root->contains("intensity_penalty"))
        {
            const auto& penalty = (*root)["intensity_penalty"];
            for (const auto& bound : penalty.value("bounds", config::Json::array()))
                if (bound.is_number())
                    model.penalty_bounds.push_back(bound.get<float>());
            model.penalty_keep = penalty.value("keep", 1.0f);
        }
        const auto& families = (*root)["families"];
        if (!families.is_object())
            return std::unexpected("families is not an object");
        for (const auto& [name, family] : families.items())
        {
            RatingModel::Family& out = model.families.emplace_back();
            out.name = name;
            for (const auto& type : family.value("types", config::Json::array()))
                if (type.is_string())
                    out.types.push_back(type.get<std::string>());
            for (std::size_t k = 0; k < kRatingNames.size(); ++k)
            {
                if (!family.contains(kRatingNames[k]))
                    continue;
                const auto weights = read_features(family[kRatingNames[k]], std::format("{} {}", name, kRatingNames[k]));
                if (!weights)
                    return std::unexpected(weights.error());
                out.weights[k] = *weights;
            }
        }
        return model;
    }

    const RatingModel& builtin_rating_model()
    {
        // Parsed once: the embedded file never changes while the program runs.
        static const RatingModel model = [] {
            const auto file = assets::embedded_file(kModelPath);
            auto parsed = file ? parse_rating_model(assets::as_text(*file)) : std::unexpected(std::string("not built in"));
            if (!parsed)
                logging::logger("simulation").error("ride ratings: {}", parsed.error());
            return parsed.value_or(RatingModel { });
        }();
        return model;
    }

    RatingFeatures rating_features(const save::TestResults& t)
    {
        return {
            1, t.max_speed, t.average_speed, t.ride_time, t.ride_length, std::max(t.max_positive_vertical / kGravity - 1, 0.0f), std::abs(t.max_negative_vertical) / kGravity, std::abs(t.max_lateral) / kGravity, t.air_time, static_cast<float>(t.drop_count), t.highest_drop, static_cast<float>(t.inversion_count), static_cast<float>(t.lift_hill_count),
        };
    }

    // An approximation of the original's ratings, not a copy: weights fitted to saved parks, so expect a ride's
    // ratings to differ from what the same ride showed in the original game.
    Ratings rate(const RatingModel& model, std::string_view type, const save::TestResults& test)
    {
        const auto in = [&](const RatingModel::Family& f) { return std::ranges::any_of(f.types, [&](const std::string& t) { return same_ignoring_case(t, type); }); };
        auto family = std::ranges::find_if(model.families, in);
        if (family == model.families.end())
            family = std::ranges::find(model.families, kFallbackFamily, &RatingModel::Family::name);
        if (family == model.families.end())
            return { };
        const RatingFeatures raw = rating_features(test);
        std::array<float, 3> sums { };
        for (std::size_t k = 0; k < sums.size(); ++k)
            for (std::size_t i = 0; i < raw.size(); ++i)
                sums[k] += family->weights[k][i] * std::min(raw[i], model.caps[i]);
        Ratings r { std::max(sums[0], 0.0f), std::max(sums[1], 0.0f), std::max(sums[2], 0.0f) };
        for (const float bound : model.penalty_bounds)
            if (r.intensity >= bound)
                r.excitement *= model.penalty_keep;
        return r;
    }

    std::optional<save::TestResults> test_lap(Coaster c, float most)
    {
        c.running = true;
        c.at = c.start;
        c.travelled = 0;
        c.speed = 0;
        save::TestResults t;
        constexpr float kStep = 0.25f;
        // The drop going on: from the highest point since the track last rose.
        float top = 0;
        float last = 0;
        bool first = true;
        const auto height = [&] {
            const auto after = std::ranges::upper_bound(c.distance, c.at);
            const auto i = static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(after - c.distance.begin() - 1, 0, static_cast<std::ptrdiff_t>(c.distance.size()) - 1));
            return c.height[i];
        };
        const auto end_drop = [&] {
            if (top - last >= kSmallestDrop)
            {
                ++t.drop_count;
                t.highest_drop = std::max(t.highest_drop, top - last);
            }
        };
        for (float time = kStep; time <= most; time += kStep)
        {
            const bool back = drive(c, kStep);
            t.max_speed = std::max(t.max_speed, std::abs(c.speed));
            const float h = height();
            if (first || h > last)
            {
                if (!first)
                    end_drop();
                top = h;
                first = false;
            }
            last = h;
            if (back)
            {
                end_drop();
                t.ride_time = time;
                t.ride_length = c.length();
                t.average_speed = t.ride_length / time;
                return t;
            }
        }
        return std::nullopt;
    }

} // namespace rct3::simulation
