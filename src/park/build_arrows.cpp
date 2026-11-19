// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/build_arrows.hpp"

#include "core/types.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace rct3::park
{

    namespace
    {

        constexpr units::Distance kStripWidth { 0.08f }; ///< Of each strip of a track arrow
        constexpr f32 kAboveRails = 0.2f;                ///< How far the track arrows float over the rails

        /// Adds the strip from `from` to `to`, `up` being the arrow's own up: both faces, so it shows from below.
        void add_strip(TerrainMesh& mesh, glm::vec3 from, glm::vec3 to, glm::vec3 up, glm::vec4 colour)
        {
            const glm::vec3 side = glm::normalize(glm::cross(to - from, up)) * (kStripWidth.value / 2);
            const auto first = static_cast<u32>(mesh.vertices.size());
            for (const glm::vec3 p : { from - side, from + side, to - side, to + side })
                mesh.vertices.push_back({ .position = p, .colour = colour, .uv = { 0, 0 }, .normal = { 0, 1, 0 } });
            for (const u32 i : { 0u, 1u, 2u, 3u, 2u, 1u, 0u, 2u, 1u, 3u, 1u, 2u })
                mesh.indices.push_back(first + i);
        }

    } // namespace

    TerrainMesh facing_arrow(i32 col, i32 row, f32 height, Side side, const save::model::TerrainGrid& grid)
    {
        // Out of the tile, in columns and rows.
        const auto [out_col, out_row] = side.step();
        TerrainMesh mesh;
        for (i32 corner = 0; corner < 4; ++corner)
        {
            const i32 c = corner % 2;
            const i32 r = corner / 2;
            // From the quad's middle, in half tiles: the outer edge is +1 along the way out.
            const i32 along = (2 * c - 1) * out_col + (2 * r - 1) * out_row;
            const i32 across = (2 * c - 1) * out_row - (2 * r - 1) * out_col;
            GroundVertex& v = mesh.vertices.emplace_back();
            v.position = { grid.world_x(col) + (static_cast<f32>(c) + 0.5f * static_cast<f32>(out_col)) * grid.tile_size_x, height, grid.world_z(row) + (static_cast<f32>(r) + 0.5f * static_cast<f32>(out_row)) * grid.tile_size_z };
            v.uv = { across > 0 ? 1.0f : 0.0f, along > 0 ? 1.0f : 0.0f };
            v.normal = { 0, 1, 0 };
        }
        mesh.indices = { 0, 1, 2, 3, 2, 1 }; // The ground's split
        mesh.batches.push_back({ GroundPaint::Surface, 0, 0, 6 });
        return mesh;
    }

    TerrainMesh track_arrows(const track::TrackTip& start, const track::TrackTip& end, bool forward)
    {
        // The pitch of each slope step, near the middle of the pitches the step takes.
        // shortcut: the track's bank is not followed, upgrade if the arrows are seen to cut through banked rails.
        constexpr std::array<f32, 4> kPitch { 0, 27, 63, 90 };
        const auto frame = [&](const track::TrackTip& tip) {
            const f32 heading = static_cast<f32>(tip.end.heading.value) * std::numbers::pi_v<f32> / 4;
            const f32 pitch = std::copysign(kPitch[static_cast<usize>(std::min(std::abs(tip.end.slope), 3))], static_cast<f32>(tip.end.slope)) * std::numbers::pi_v<f32> / 180;
            const glm::vec3 level { std::cos(heading), 0, -std::sin(heading) }; // Heading 0 along +x, 2 along -z
            const glm::vec3 forward_way = level * std::cos(pitch) + glm::vec3 { 0, std::sin(pitch), 0 };
            const glm::vec3 right = glm::normalize(glm::cross(level, glm::vec3 { 0, 1, 0 }));
            const glm::vec3 up = glm::cross(right, forward_way);
            return std::array { forward_way, right, up, tip.position + up * kAboveRails };
        };
        TerrainMesh mesh;
        {
            const auto [f, r, u, b] = frame(start);
            constexpr glm::vec4 kWhite { 1, 1, 1, 1 };
            add_strip(mesh, b - f, b + f, u, kWhite);
            add_strip(mesh, b + f, b + 0.6f * f + 0.25f * r, u, kWhite);
            add_strip(mesh, b + f, b + 0.6f * f - 0.25f * r, u, kWhite);
        }
        {
            const auto [f, r, u, b] = frame(end);
            const glm::vec4 colour = forward ? glm::vec4 { 0, 1, 0, 1 } : glm::vec4 { 1, 0, 0, 1 };
            const glm::vec3 left = b - 0.7f * f - 0.5f * r;
            const glm::vec3 right = b - 0.7f * f + 0.5f * r;
            add_strip(mesh, b, right, u, colour);
            add_strip(mesh, right, left, u, colour);
            add_strip(mesh, left, b, u, colour);
        }
        mesh.batches.push_back({ GroundPaint::Surface, 0, 0, static_cast<u32>(mesh.indices.size()) });
        return mesh;
    }

} // namespace rct3::park
