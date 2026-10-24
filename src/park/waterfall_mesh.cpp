// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/waterfall_mesh.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::park
{

    namespace
    {

        constexpr int kRows = 12;              ///< Rows down the sheet.
        constexpr float kLeadIn = 1.5f;        ///< How far back over the upper lake the sheet starts.
        constexpr float kMinReach = 1.0f;      ///< Least distance out from the edge the water lands.
        constexpr float kSidesGap = 0.05f;     ///< The side streaks lie this far outside the sheet.
        constexpr float kSheetRepeat = 8.0f;   ///< Park units the sheet's picture covers.
        constexpr float kFallSpeed = 6.0f;     ///< How fast the sheet's picture runs, units a second.
        constexpr float kFoamMargin = 3.0f;    ///< Foam reaches this far round where the water lands.
        constexpr float kFoamRepeat = 8.0f;    ///< Park units the foam's picture covers.
        constexpr float kFoamSpeed = 1.0f;     ///< How fast the foam drifts away, units a second.
        constexpr float kFoamLift = 0.05f;     ///< Foam lies this high over the lower lake.
        constexpr float kFrothHeight = 3.0f;   ///< Tallest the froth stands.
        constexpr float kFrothRepeat = 8.0f;   ///< Park units across the froth's picture covers.
        constexpr float kFrothPicture = 0.55f; ///< Part of the froth's picture, from the top, the quad shows.
        constexpr glm::vec3 kUp { 0.0f, 1.0f, 0.0f };

        glm::vec3 vec(const save::WorldPoint3& p)
        {
            return { p.x, p.y, p.z };
        }

        /// The shape of a fall: where its edge is, which way it runs and how far it goes out and down.
        struct Shape
        {
            glm::vec3 lip { 0.0f };    ///< Middle of the edge
            glm::vec3 across { 0.0f }; ///< From the left end of the edge to the right
            glm::vec3 flow { 1.0f, 0.0f, 0.0f };
            float reach = kMinReach;
            float drop = 0;
        };

        Shape shape_of(const save::Waterfall& fall)
        {
            Shape s { .lip = (vec(fall.lip_left) + vec(fall.lip_right)) / 2.0f, .across = vec(fall.lip_right) - vec(fall.lip_left) };
            s.across.y = 0;
            glm::vec3 flow { fall.flow.x, 0.0f, fall.flow.z };
            if (glm::length(flow) < 1e-4f)
                flow = { fall.bottom.x - s.lip.x, 0.0f, fall.bottom.z - s.lip.z };
            if (glm::length(flow) >= 1e-4f)
                s.flow = glm::normalize(flow);
            s.reach = std::max(glm::dot(vec(fall.bottom) - s.lip, s.flow), kMinReach);
            s.drop = std::max(s.lip.y - fall.bottom.y, 0.0f);
            return s;
        }

        glm::vec3 point(const Shape& s, float along)
        {
            return s.lip + s.flow * (s.reach * along) - kUp * (s.drop * along * along);
        }

        void quad(ObjectMesh& mesh, std::uint32_t a)
        {
            mesh.indices.insert(mesh.indices.end(), { a, a + 1, a + 2, a + 1, a + 3, a + 2 });
        }

        /// The sheet, or the streaks at its sides `gap` outside it, as rows from just behind the edge to where
        /// it lands.
        void sheet(ObjectMesh& mesh, const Shape& s, float seconds, float gap, float across_repeats)
        {
            const glm::vec3 half = s.across / 2.0f;
            float run = 0;
            glm::vec3 last = s.lip - s.flow * kLeadIn;
            for (int k = -1; k <= kRows; ++k)
            {
                const float along = static_cast<float>(std::max(k, 0)) / kRows;
                const glm::vec3 middle = k < 0 ? last : point(s, along);
                run += glm::distance(middle, last);
                last = middle;
                // Square to the water's way down, on the side it falls away from.
                const glm::vec3 down = s.flow * s.reach - kUp * (2.0f * s.drop * along);
                glm::vec3 normal = glm::cross(half, down);
                normal = glm::length(normal) > 0 ? glm::normalize(normal) : kUp;
                if (glm::dot(normal, s.flow) + normal.y < 0)
                    normal = -normal;
                const glm::vec3 at = middle + normal * gap;
                const float v = (run - seconds * kFallSpeed) / kSheetRepeat;
                mesh.vertices.push_back({ .position = at - half, .uv = { 0.0f, v }, .normal = normal });
                mesh.vertices.push_back({ .position = at + half, .uv = { across_repeats, v }, .normal = normal });
            }
            const auto first = static_cast<std::uint32_t>(mesh.vertices.size()) - 2 * (kRows + 2);
            for (std::uint32_t r = 0; r <= kRows; ++r)
                quad(mesh, first + r * 2);
        }

    } // namespace

    glm::vec3 waterfall_point(const save::Waterfall& fall, float along)
    {
        return point(shape_of(fall), along);
    }

    void waterfall_mesh(std::span<const save::Waterfall> falls, float seconds, ObjectMesh& mesh)
    {
        mesh.vertices.clear();
        mesh.indices.clear();
        mesh.owners.clear();
        std::size_t batches = 0; // The batches are reused, so their names keep their memory frame to frame
        for (std::size_t picture = 0; picture < kWaterfallPictures.size(); ++picture)
        {
            const auto first = static_cast<std::uint32_t>(mesh.indices.size());
            for (const save::Waterfall& fall : falls)
            {
                const Shape s = shape_of(fall);
                const float width = glm::length(s.across);
                if (width <= 0)
                    continue;
                const glm::vec3 half = s.across / 2.0f;
                const glm::vec3 land = point(s, 1.0f);
                const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
                switch (picture)
                {
                    case 0:
                        sheet(mesh, s, seconds, 0.0f, std::max(1.0f, std::round(width / kSheetRepeat)));
                        break;
                    case 1:
                        sheet(mesh, s, seconds, kSidesGap, 1.0f);
                        break;
                    case 2:
                    {
                        const glm::vec3 side = glm::normalize(half) * (width / 2.0f + kFoamMargin);
                        const glm::vec3 out = s.flow * kFoamMargin * 2.0f;
                        const glm::vec3 centre = land + kUp * kFoamLift;
                        for (const glm::vec3 corner : { centre - side - out, centre + side - out, centre - side + out, centre + side + out })
                        {
                            const glm::vec3 d = corner - centre;
                            const glm::vec2 uv { glm::dot(d, glm::normalize(half)) / kFoamRepeat, (glm::dot(d, s.flow) - seconds * kFoamSpeed) / kFoamRepeat };
                            mesh.vertices.push_back({ .position = corner, .uv = uv, .normal = kUp });
                        }
                        quad(mesh, base);
                        break;
                    }
                    default:
                    {
                        const float height = std::min(kFrothHeight, s.drop);
                        const float across = std::max(1.0f, std::round(width / kFrothRepeat));
                        mesh.vertices.push_back({ .position = land - half + kUp * height, .uv = { 0.0f, 0.0f }, .normal = -s.flow });
                        mesh.vertices.push_back({ .position = land + half + kUp * height, .uv = { across, 0.0f }, .normal = -s.flow });
                        mesh.vertices.push_back({ .position = land - half, .uv = { 0.0f, kFrothPicture }, .normal = -s.flow });
                        mesh.vertices.push_back({ .position = land + half, .uv = { across, kFrothPicture }, .normal = -s.flow });
                        if (height > 0)
                            quad(mesh, base);
                        break;
                    }
                }
            }
            const auto count = static_cast<std::uint32_t>(mesh.indices.size()) - first;
            if (count == 0)
                continue;
            if (batches == mesh.batches.size())
                mesh.batches.emplace_back();
            ObjectBatch& batch = mesh.batches[batches++];
            batch.texture = kWaterfallPictures[picture].key;
            batch.picture = kWaterfallPictures[picture].key;
            batch.blend = PartBlend::Blended;
            batch.first = first;
            batch.count = count;
        }
        mesh.batches.resize(batches);
    }

} // namespace rct3::park
