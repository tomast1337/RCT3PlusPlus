// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/build_arrows.hpp"

#include <array>
#include <cmath>
#include <numbers>

namespace rct3::park
{

    namespace
    {

        constexpr float kStripWidth = 0.08f; ///< Of each strip of a track arrow, in world units
        constexpr float kAboveRails = 0.2f;  ///< How far the track arrows float over the rails

        /// Adds the strip from `from` to `to`, `up` being the arrow's own up: both faces, so it shows from below.
        void add_strip(TerrainMesh& mesh, glm::vec3 from, glm::vec3 to, glm::vec3 up, glm::vec4 colour)
        {
            const glm::vec3 side = glm::normalize(glm::cross(to - from, up)) * (kStripWidth / 2);
            const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
            for (const glm::vec3 p : { from - side, from + side, to - side, to + side })
                mesh.vertices.push_back({ .position = p, .colour = colour, .uv = { 0, 0 }, .normal = { 0, 1, 0 } });
            for (const std::uint32_t i : { 0u, 1u, 2u, 3u, 2u, 1u, 0u, 2u, 1u, 3u, 1u, 2u })
                mesh.indices.push_back(first + i);
        }

    } // namespace

    TerrainMesh facing_arrow(std::int32_t col, std::int32_t row, float height, std::uint8_t facing, const save::TerrainGrid& grid)
    {
        // Out of the tile, by facing, in columns and rows.
        constexpr std::array<std::array<int, 2>, 4> kOut { { { 0, -1 }, { -1, 0 }, { 0, 1 }, { 1, 0 } } };
        const auto [out_col, out_row] = kOut[facing % 4];
        TerrainMesh mesh;
        for (int corner = 0; corner < 4; ++corner)
        {
            const int c = corner % 2;
            const int r = corner / 2;
            // From the quad's middle, in half tiles: the outer edge is +1 along the way out.
            const int along = (2 * c - 1) * out_col + (2 * r - 1) * out_row;
            const int across = (2 * c - 1) * out_row - (2 * r - 1) * out_col;
            GroundVertex& v = mesh.vertices.emplace_back();
            v.position = { grid.world_x(col) + (static_cast<float>(c) + 0.5f * static_cast<float>(out_col)) * grid.tile_size_x, height, grid.world_z(row) + (static_cast<float>(r) + 0.5f * static_cast<float>(out_row)) * grid.tile_size_z };
            v.uv = { across > 0 ? 1.0f : 0.0f, along > 0 ? 1.0f : 0.0f };
            v.normal = { 0, 1, 0 };
        }
        mesh.indices = { 0, 1, 2, 3, 2, 1 }; // The ground's split
        mesh.batches.push_back({ GroundPaint::Surface, 0, 0, 6 });
        return mesh;
    }

    TerrainMesh track_arrows(const TrackTip& start, const TrackTip& end, bool forward)
    {
        // The pitch of each slope step, near the middle of the pitches the step takes.
        // shortcut: the track's bank is not followed, upgrade if the arrows are seen to cut through banked rails.
        constexpr std::array<float, 4> kPitch { 0, 27, 63, 90 };
        const auto frame = [&](const TrackTip& tip) {
            const float heading = static_cast<float>(tip.end.heading) * std::numbers::pi_v<float> / 4;
            const float pitch = std::copysign(kPitch[static_cast<std::size_t>(std::min(std::abs(tip.end.slope), 3))], static_cast<float>(tip.end.slope)) * std::numbers::pi_v<float> / 180;
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
        mesh.batches.push_back({ GroundPaint::Surface, 0, 0, static_cast<std::uint32_t>(mesh.indices.size()) });
        return mesh;
    }

} // namespace rct3::park
