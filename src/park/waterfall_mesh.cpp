// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/waterfall_mesh.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace rct3::park
{

    namespace
    {

        constexpr int kRows = 12;                           ///< Rows down the sheet
        constexpr units::Distance kLeadIn { 1.5f };         ///< How far back over the upper lake the sheet starts
        constexpr units::Distance kMinReach { 1.0f };       ///< Least distance out from the edge the water lands
        constexpr units::Distance kFoamLift { 0.05f };      ///< Foam lies this high over the lower lake
        constexpr float kGravity = 9.81f;                   ///< World units are metres
        constexpr units::Distance kSameEdge { 0.5f };       ///< Falls this close in height and place pour over one edge
        constexpr units::Distance kFoamLeast { 3.0f };      ///< Chosen by eye: how far foam reaches round the smallest fall
        constexpr units::Distance kFoamMost { 14.0f };      ///< And round the tallest
        constexpr float kFoamPerDrop = 0.3f;                ///< Chosen by eye: units further per unit of drop
        constexpr units::Distance kMistFrom { 10.0f };      ///< Chosen by eye: drops under this raise no mist
        constexpr units::Distance kMistFull { 40.0f };      ///< And from this the mist is as thick as it gets
        constexpr float kMistPerDrop = 0.22f;               ///< Chosen by eye: a puff's size per unit of drop
        constexpr units::Distance kMistSize { 12.0f };      ///< Chosen by eye: and the most
        constexpr float kMistOut = 0.35f;                   ///< Chosen by eye: of its size, how far a puff stands out from the cliff, so it does not cut into it
        constexpr int kMistMost = 10;                       ///< Puffs along one fall, in each row
        constexpr float kDropletsPerUnit = 1.5f;            ///< Chosen by eye: droplets thrown per unit of a fall's width
        constexpr int kDropletsLeast = 8;                   ///< Chosen by eye: the fewest a fall throws
        constexpr int kDropletsMost = 24;                   ///< Chosen by eye: and the most
        constexpr units::Distance kDropletRise { 0.07f };   ///< Chosen by eye: how high a droplet is thrown, per unit of drop
        constexpr units::Distance kDropletLow { 0.4f };     ///< Chosen by eye: the least a small fall throws one
        constexpr units::Distance kDropletHigh { 2.5f };    ///< Chosen by eye: and the most a tall one does
        constexpr units::Distance kDropletSize { 0.06f };   ///< Chosen by eye: a droplet's size off the smallest fall
        constexpr float kDropletPerDrop = 0.004f;           ///< Chosen by eye: units bigger per unit of drop
        constexpr units::Distance kDropletLargest { 0.2f }; ///< Chosen by eye: and the biggest
        constexpr glm::vec3 kUp { 0.0f, 1.0f, 0.0f };
        constexpr std::array<glm::vec2, 4> kCorners { glm::vec2 { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } };

        glm::vec3 vec(const save::model::WorldPoint3& p)
        {
            return { p.x, p.y, p.z };
        }

        /// The shape of a fall: where its edge is, which way it runs and how far it goes out and down.
        struct Shape
        {
            glm::vec3 lip { 0.0f };    ///< Middle of the edge
            glm::vec3 across { 0.0f }; ///< From the left end of the edge to the right
            glm::vec3 flow { 1.0f, 0.0f, 0.0f };
            float reach = kMinReach.value;
            float drop = 0;
        };

        Shape shape_of(const save::model::Waterfall& fall)
        {
            Shape s { .lip = (vec(fall.lip_left) + vec(fall.lip_right)) / 2.0f, .across = vec(fall.lip_right) - vec(fall.lip_left) };
            s.across.y = 0;
            glm::vec3 flow { fall.flow.x, 0.0f, fall.flow.z };
            if (glm::length(flow) < 1e-4f)
                flow = { fall.bottom.x - s.lip.x, 0.0f, fall.bottom.z - s.lip.z };
            if (glm::length(flow) >= 1e-4f)
                s.flow = glm::normalize(flow);
            s.reach = std::max(glm::dot(vec(fall.bottom) - s.lip, s.flow), kMinReach.value);
            s.drop = std::max(s.lip.y - fall.bottom.y, 0.0f);
            return s;
        }

        /// `s` less the stretch of its edge that `other` already pours over, keeping the longer piece left; none
        /// when too little is left.
        std::optional<Shape> beside(Shape s, const Shape& other)
        {
            const float width = glm::length(s.across);
            const float other_width = glm::length(other.across);
            if (other_width <= 0 || std::abs(s.lip.y - other.lip.y) > kSameEdge.value || std::abs(s.drop - other.drop) > kSameEdge.value || glm::dot(s.flow, other.flow) < 0.95f)
                return s;
            const glm::vec3 edge = other.across / other_width;
            const glm::vec3 off = s.lip - other.lip;
            const float along = glm::dot(off, edge);
            const glm::vec3 aside = off - edge * along;
            if (glm::length(aside) > kSameEdge.value)
                return s;
            // The edge's two ends, along the other's; the pieces either side of the other.
            const float start = along - width / 2;
            const float end = along + width / 2;
            const float before = std::min(end, -other_width / 2) - start;
            const float after = end - std::max(start, other_width / 2);
            const bool first = before >= after;
            const float from = first ? start : std::max(start, other_width / 2);
            const float to = first ? std::min(end, -other_width / 2) : end;
            if (to - from < kSameEdge.value)
                return std::nullopt;
            s.lip = other.lip + aside + edge * ((from + to) / 2);
            s.across = edge * ((to - from) * (glm::dot(s.across, edge) < 0 ? -1.0f : 1.0f));
            return s;
        }

        glm::vec3 point(const Shape& s, float along)
        {
            return s.lip + s.flow * (s.reach * along) - kUp * (s.drop * along * along);
        }

        void quad(WaterfallMesh& mesh, std::uint32_t a)
        {
            mesh.indices.insert(mesh.indices.end(), { a, a + 1, a + 2, a + 1, a + 3, a + 2 });
        }

        /// A steady number 0 to 1 for `k` and `salt`.
        float own_number(std::uint32_t k, std::uint32_t salt)
        {
            const std::uint32_t h = (k * 9 + salt) * 2654435761U;
            return static_cast<float>(h >> 8) / static_cast<float>(1U << 24);
        }

    } // namespace

    glm::vec3 waterfall_point(const save::model::Waterfall& fall, float along)
    {
        return point(shape_of(fall), along);
    }

    units::Seconds fall_seconds(units::Distance drop)
    {
        return { std::sqrt(2.0f * std::max(drop.value, 0.0f) / kGravity) };
    }

    void waterfall_mesh(std::span<const save::model::Waterfall> falls, std::span<const FallLakes> lakes, WaterfallDetail detail, WaterfallMesh& mesh)
    {
        mesh.vertices.clear();
        mesh.indices.clear();
        for (std::size_t part = 0; part < mesh.parts.size(); ++part)
        {
            const auto first = static_cast<std::uint32_t>(mesh.indices.size());
            for (std::size_t f = 0; f < falls.size(); ++f)
            {
                std::optional<Shape> trimmed = shape_of(falls[f]);
                for (std::size_t g = 0; g < f && trimmed; ++g)
                    trimmed = beside(*trimmed, shape_of(falls[g]));
                if (!trimmed)
                    continue;
                const Shape& s = *trimmed;
                const float width = glm::length(s.across);
                if (width <= 0 || s.drop <= 0)
                    continue;
                const FallLakes colours = f < lakes.size() ? lakes[f] : FallLakes { };
                const glm::vec3 half = s.across / 2.0f;
                const glm::vec3 side = half / (width / 2.0f);
                const glm::vec3 land = point(s, 1.0f);
                const float seconds = fall_seconds({ s.drop }).value;
                switch (static_cast<FallPart>(part))
                {
                    case FallPart::Foam:
                    {
                        const float reach = std::clamp(kFoamLeast.value + kFoamPerDrop * s.drop, kFoamLeast.value, kFoamMost.value);
                        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
                        for (const glm::vec2 corner : kCorners)
                        {
                            const glm::vec2 local { corner.x * (width / 2.0f + reach), corner.y * reach };
                            mesh.vertices.push_back({ .position = land + kUp * kFoamLift.value + side * local.x + s.flow * local.y, .colour = colours.lower, .uv = local, .normal = { width / 2.0f, s.drop, reach } });
                        }
                        quad(mesh, base);
                        break;
                    }
                    case FallPart::Sheet:
                    {
                        glm::vec3 last = s.lip - s.flow * kLeadIn.value;
                        for (int k = -1; k <= kRows; ++k)
                        {
                            const float along = static_cast<float>(std::max(k, 0)) / kRows;
                            const glm::vec3 middle = k < 0 ? last : point(s, along);
                            last = middle;
                            // The water runs out at a steady speed, so its time since the edge is `along` of the whole.
                            const float since = k < 0 ? -kLeadIn.value * seconds / s.reach : along * seconds;
                            const glm::vec4 colour { glm::vec3(glm::mix(colours.upper, colours.lower, along)), k < 0 ? 0.0f : 1.0f }; // Fading in from behind the edge
                            for (const float end : { -1.0f, 1.0f })
                                mesh.vertices.push_back({ .position = middle + half * end, .colour = colour, .uv = { end * width / 2.0f, since }, .normal = { width / 2.0f, seconds, s.drop } });
                        }
                        const auto base = static_cast<std::uint32_t>(mesh.vertices.size()) - 2 * (kRows + 2);
                        for (std::uint32_t r = 0; r <= kRows; ++r)
                            quad(mesh, base + r * 2);
                        break;
                    }
                    case FallPart::Mist:
                    {
                        const float thick = std::clamp((s.drop - kMistFrom.value) / (kMistFull.value - kMistFrom.value), 0.0f, 1.0f);
                        if (detail >= WaterfallDetail::Medium && thick > 0)
                        {
                            const float size = std::min(kMistPerDrop * s.drop, kMistSize.value);
                            const int count = std::clamp(static_cast<int>(std::round(width / (0.5f * size))) + 1, 2, kMistMost);
                            for (int row = 0; row < (detail >= WaterfallDetail::High ? 2 : 1); ++row)
                                for (int k = 0; k < count; ++k)
                                {
                                    const float across = (static_cast<float>(k) + 0.5f * static_cast<float>(row)) / static_cast<float>(count - 1) - 0.5f;
                                    const glm::vec3 middle = land + side * (across * width) + s.flow * (size * (kMistOut + 0.15f * static_cast<float>(row))) + kUp * (0.25f * size);
                                    const float number = own_number(static_cast<std::uint32_t>(k * 2 + row), static_cast<std::uint32_t>(f));
                                    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
                                    for (const glm::vec2 corner : kCorners)
                                        mesh.vertices.push_back({ .position = middle, .colour = colours.lower, .uv = corner, .normal = { size * (row == 0 ? 1.0f : 1.4f), number, thick } });
                                    quad(mesh, base);
                                }
                        }
                        if (detail < WaterfallDetail::Ultra)
                            break;
                        // Droplets thrown up and out where the water lands, each on a loop of its own.
                        const int count = std::clamp(static_cast<int>(std::round(width * kDropletsPerUnit)), kDropletsLeast, kDropletsMost);
                        const float rise = std::clamp(kDropletRise.value * s.drop, kDropletLow.value, kDropletHigh.value);
                        const float size = std::min(kDropletSize.value + kDropletPerDrop * s.drop, kDropletLargest.value);
                        for (int k = 0; k < count; ++k)
                        {
                            const auto n = static_cast<std::uint32_t>(k);
                            const auto salt = static_cast<std::uint32_t>(f) * 7;
                            const glm::vec3 from = land + side * ((own_number(n, salt) - 0.5f) * width) + kUp * kFoamLift.value;
                            const float up = std::sqrt(2.0f * kGravity * rise * (0.4f + 0.6f * own_number(n, salt + 1)));
                            const glm::vec3 speed = kUp * up + s.flow * (up * (0.2f + 0.5f * own_number(n, salt + 2))) + side * (up * 0.4f * (own_number(n, salt + 3) - 0.5f));
                            const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
                            for (const glm::vec2 corner : kCorners)
                                mesh.vertices.push_back({ .position = from, .colour = { speed, 1.0f }, .uv = corner, .normal = { size, own_number(n, salt + 4), -1.0f } });
                            quad(mesh, base);
                        }
                        break;
                    }
                }
            }
            mesh.parts[part] = { .first = first, .count = static_cast<std::uint32_t>(mesh.indices.size()) - first };
        }
    }

} // namespace rct3::park
