// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/waterfall_mesh.hpp"

#include <algorithm>
#include <array>
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
        constexpr float kFrothHeight = 2.0f;   ///< Tallest the froth stands.
        constexpr float kFrothRepeat = 8.0f;   ///< Park units across the froth's picture covers.
        constexpr float kFrothPicture = 0.55f; ///< Part of the froth's picture, from the top, the quad shows.
        constexpr float kFrothMargin = 1.0f;   ///< Froth reaches this far past each side of the sheet.
        constexpr float kFade = 0.25f;         ///< Part of foam and froth at each edge that fades out.
        constexpr float kSprayGap = 8.0f;      ///< Units of a fall's width each spray covers.
        constexpr float kSpraySize = 2.0f;     ///< Times the effect's own particle sizes, made for small splashes.
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

        /// A flat patch from `corner` spanning `a` and `b`, cut in three along `a` (and along `b` when `fade_b`)
        /// so it fades out to nothing at those edges; `uv_of` gives each point's picture coordinates.
        void grid(ObjectMesh& mesh, glm::vec3 corner, glm::vec3 a, glm::vec3 b, bool fade_b, glm::vec3 normal, auto&& uv_of)
        {
            constexpr std::array<float, 4> kCuts { 0.0f, kFade, 1.0f - kFade, 1.0f };
            constexpr std::array<float, 2> kEnds { 0.0f, 1.0f };
            const std::span<const float> along_b = fade_b ? std::span<const float>(kCuts) : std::span<const float>(kEnds);
            const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
            for (std::size_t j = 0; j < along_b.size(); ++j)
                for (std::size_t i = 0; i < kCuts.size(); ++i)
                {
                    const bool edge = i == 0 || i + 1 == kCuts.size() || (fade_b && (j == 0 || j + 1 == along_b.size()));
                    const glm::vec3 p = corner + a * kCuts[i] + b * along_b[j];
                    mesh.vertices.push_back({ .position = p, .colour = { 1.0f, 1.0f, 1.0f, edge ? 0.0f : 1.0f }, .uv = uv_of(p), .normal = normal });
                }
            const auto stride = static_cast<std::uint32_t>(kCuts.size());
            for (std::uint32_t j = 0; j + 1 < along_b.size(); ++j)
                for (std::uint32_t i = 0; i + 1 < stride; ++i)
                {
                    const std::uint32_t at = base + j * stride + i;
                    mesh.indices.insert(mesh.indices.end(), { at, at + 1, at + stride, at + 1, at + stride + 1, at + stride });
                }
        }

    } // namespace

    float waterfall_layer_scroll(const WaterfallPicture& picture, float seconds)
    {
        const float s = -seconds * picture.layer_lag; // v falls as the water runs
        return s - std::floor(s);
    }

    glm::vec3 waterfall_point(const save::Waterfall& fall, float along)
    {
        return point(shape_of(fall), along);
    }

    std::vector<PlacedEffect> waterfall_spray(std::span<const save::Waterfall> falls)
    {
        std::vector<PlacedEffect> sprays;
        for (std::size_t i = 0; i < falls.size(); ++i)
        {
            const Shape s = shape_of(falls[i]);
            const glm::vec3 start = point(s, 1.0f) + kUp * kFoamLift - s.across / 2.0f;
            const int count = std::max(1, static_cast<int>(std::round(glm::length(s.across) / kSprayGap)));
            for (int k = 0; k < count; ++k)
                sprays.push_back({ .effect = kWaterfallSpray, .position = start + s.across * ((static_cast<float>(k) + 0.5f) / static_cast<float>(count)), .object = i, .size = kSpraySize });
        }
        return sprays;
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
                const glm::vec3 side = half / (width / 2.0f);
                const glm::vec3 land = point(s, 1.0f);
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
                        const glm::vec3 across = side * (width + 2 * kFoamMargin);
                        const glm::vec3 out = s.flow * (4 * kFoamMargin);
                        const glm::vec3 corner = land + kUp * kFoamLift - across / 2.0f - out / 2.0f;
                        grid(mesh, corner, across, out, true, kUp, [&](glm::vec3 p) {
                            const glm::vec3 d = p - land;
                            return glm::vec2 { glm::dot(d, side) / kFoamRepeat, (glm::dot(d, s.flow) - seconds * kFoamSpeed) / kFoamRepeat };
                        });
                        break;
                    }
                    default:
                    {
                        const float height = std::min(kFrothHeight, s.drop);
                        if (height <= 0)
                            break;
                        const float repeats = std::max(1.0f, std::round(width / kFrothRepeat));
                        const glm::vec3 across = s.across + side * (2 * kFrothMargin);
                        const glm::vec3 corner = land - across / 2.0f + kUp * height;
                        grid(mesh, corner, across, -kUp * height, false, -s.flow, [&](glm::vec3 p) { return glm::vec2 { glm::dot(p - corner, side) / glm::length(across) * repeats, (corner.y - p.y) / height * kFrothPicture }; });
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
