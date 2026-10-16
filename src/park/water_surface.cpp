// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/water_surface.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace rct3::park
{

    WaterSurface water_surface(std::span<const WaterVertex> water)
    {
        std::map<float, std::vector<GroundVertex>> by_level;
        for (std::size_t i = 0; i + 3 <= water.size(); i += 3)
        {
            std::vector<GroundVertex>& run = by_level[water[i].position.y];
            for (std::size_t k = i; k < i + 3; ++k)
                run.push_back({ .position = water[k].position, .colour = water[k].colour, .uv = { water[k].depth, 0.0f } });
        }
        WaterSurface surface;
        for (const auto& [level, run] : by_level)
        {
            WaterBatch batch { level, static_cast<std::uint32_t>(surface.vertices.size()), static_cast<std::uint32_t>(run.size()), run.front().position, run.front().position };
            for (const GroundVertex& v : run)
            {
                batch.low = glm::min(batch.low, v.position);
                batch.high = glm::max(batch.high, v.position);
            }
            surface.vertices.insert(surface.vertices.end(), run.begin(), run.end());
            surface.batches.push_back(batch);
        }
        return surface;
    }

    namespace
    {
        /// How much of the view a triangle covers, in units of the whole view. One with a corner behind the eye
        /// is near and large, so it counts as the whole view.
        float view_share(const glm::mat4& view_projection, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
        {
            std::array<glm::vec2, 3> p;
            const std::array<glm::vec3, 3> corners { a, b, c };
            for (std::size_t i = 0; i < 3; ++i)
            {
                const glm::vec4 clip = view_projection * glm::vec4(corners[i], 1.0f);
                if (clip.w <= 0)
                    return 1;
                // Corners far off to the side are pulled in, which keeps huge triangles from counting more
                // than the view holds.
                p[i] = glm::clamp(glm::vec2(clip) / clip.w, glm::vec2(-2.0f), glm::vec2(2.0f));
            }
            if (std::max({ p[0].x, p[1].x, p[2].x }) < -1 || std::min({ p[0].x, p[1].x, p[2].x }) > 1 || std::max({ p[0].y, p[1].y, p[2].y }) < -1 || std::min({ p[0].y, p[1].y, p[2].y }) > 1)
                return 0;
            const glm::vec2 u = p[1] - p[0];
            const glm::vec2 v = p[2] - p[0];
            return std::abs(u.x * v.y - u.y * v.x) / 8; // The view is 2 by 2, and a triangle half the parallelogram
        }
    } // namespace

    std::vector<float> reflection_levels(const WaterSurface& surface, const glm::mat4& view_projection, glm::vec3 eye, std::size_t count)
    {
        struct Scored
        {
            float level;
            float share;
        };
        std::vector<Scored> scored;
        for (const WaterBatch& batch : surface.batches)
        {
            if (batch.level >= eye.y)
                continue;
            float share = 0;
            for (std::uint32_t i = batch.first; i + 3 <= batch.first + batch.count; i += 3)
                share += view_share(view_projection, surface.vertices[i].position, surface.vertices[i + 1].position, surface.vertices[i + 2].position);
            if (share > 0)
                scored.push_back({ batch.level, share });
        }
        std::ranges::stable_sort(scored, [](const Scored& a, const Scored& b) { return a.share > b.share; });
        std::vector<float> levels;
        for (const Scored& s : scored)
        {
            if (levels.size() == count)
                break;
            // Water that would borrow a chosen reflection anyway does not need its own.
            if (!reflection_for(s.level, levels))
                levels.push_back(s.level);
        }
        return levels;
    }

    std::optional<std::size_t> reflection_for(float level, std::span<const float> levels)
    {
        std::optional<std::size_t> best;
        for (std::size_t i = 0; i < levels.size(); ++i)
        {
            const float gap = std::abs(levels[i] - level);
            if (gap <= kReflectionReach && (!best || gap < std::abs(levels[*best] - level)))
                best = i;
        }
        return best;
    }

    glm::mat4 mirror(float level)
    {
        glm::mat4 m(1.0f);
        m[1][1] = -1;
        m[3][1] = 2 * level;
        return m;
    }

    glm::mat4 oblique_clip(const glm::mat4& transform, glm::vec4 plane)
    {
        // Clip depth is row 2 of the matrix applied to a point; making it a multiple of the plane puts depth 0
        // on the plane. The multiple is chosen so that the far corner of the view, on the plane's kept side,
        // still lands on depth 1 (depth equal to w).
        const glm::mat4 back = glm::inverse(transform);
        const glm::vec4 in_clip = glm::transpose(back) * plane;
        const glm::vec4 corner = back * glm::vec4(in_clip.x >= 0 ? 1.0f : -1.0f, in_clip.y >= 0 ? 1.0f : -1.0f, 1.0f, 1.0f);
        const glm::vec4 w_row { transform[0][3], transform[1][3], transform[2][3], transform[3][3] };
        const glm::vec4 depth_row = plane * (glm::dot(w_row, corner) / glm::dot(plane, corner));
        glm::mat4 m = transform;
        for (glm::length_t c = 0; c < 4; ++c)
            m[c][2] = depth_row[c];
        return m;
    }

    glm::mat4 reflection_transform(const glm::mat4& view_projection, float level)
    {
        return oblique_clip(view_projection * mirror(level), { 0.0f, 1.0f, 0.0f, -(level - kReflectionClipBias) });
    }

    std::optional<float> water_hit(const Ray& ray, const WaterSurface& surface)
    {
        std::optional<float> nearest;
        for (const WaterBatch& batch : surface.batches)
        {
            // The surface is level, so the ray meets each batch's plane once at most.
            if (std::abs(ray.direction.y) < 1e-6f)
                continue;
            const float t = (batch.level - ray.origin.y) / ray.direction.y;
            if (t < 0 || (nearest && t >= *nearest))
                continue;
            const glm::vec3 at = ray.origin + ray.direction * t;
            if (at.x < batch.low.x || at.x > batch.high.x || at.z < batch.low.z || at.z > batch.high.z)
                continue;
            for (std::uint32_t i = batch.first; i + 3 <= batch.first + batch.count; i += 3)
            {
                const glm::vec2 a { surface.vertices[i].position.x, surface.vertices[i].position.z };
                const glm::vec2 b { surface.vertices[i + 1].position.x, surface.vertices[i + 1].position.z };
                const glm::vec2 c { surface.vertices[i + 2].position.x, surface.vertices[i + 2].position.z };
                const glm::vec2 p { at.x, at.z };
                const auto side = [](glm::vec2 from, glm::vec2 to, glm::vec2 q) { return (to.x - from.x) * (q.y - from.y) - (to.y - from.y) * (q.x - from.x); };
                const float s0 = side(a, b, p);
                const float s1 = side(b, c, p);
                const float s2 = side(c, a, p);
                if ((s0 >= 0 && s1 >= 0 && s2 >= 0) || (s0 <= 0 && s1 <= 0 && s2 <= 0))
                {
                    nearest = t;
                    break;
                }
            }
        }
        return nearest;
    }

} // namespace rct3::park
