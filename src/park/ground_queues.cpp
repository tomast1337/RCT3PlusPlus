// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground_queues.hpp"

#include "park/ground_paths.hpp"

#include <algorithm>
#include <limits>
#include <map>

namespace rct3::park
{

    std::optional<TileSide> side_of(std::uint8_t direction) noexcept
    {
        switch (direction)
        {
            case 0:
                return TileSide::Left;
            case 1:
                return TileSide::Right;
            case 2:
                return TileSide::Back;
            case 3:
                return TileSide::Front;
            default:
                return std::nullopt;
        }
    }

    std::array<int, 2> side_step(TileSide side) noexcept
    {
        switch (side)
        {
            case TileSide::Back:
                return { 0, -1 };
            case TileSide::Right:
                return { 1, 0 };
            case TileSide::Front:
                return { 0, 1 };
            case TileSide::Left:
                return { -1, 0 };
        }
        return { 0, 0 };
    }

    QueueShape queue_shape(std::uint8_t start, std::uint8_t end) noexcept
    {
        const auto in = side_of(start);
        const auto out = side_of(end);
        if (!in || !out)
            return { };
        const int a = static_cast<int>(*in);
        const int b = static_cast<int>(*out);
        const auto turns = static_cast<std::uint8_t>(a);
        if (a == b || (a + 2) % 4 == b)
            return { QueueCell::Straight, static_cast<std::uint8_t>(a % 2) };
        // Turned so the back side faces where the line comes in; it goes out one side or the other.
        return { (a + 3) % 4 == b ? QueueCell::TurnLeftSide : QueueCell::TurnRightSide, turns };
    }

    bool is_ground_queue(const save::PathTile& path) noexcept
    {
        return path.kind == save::PathKind::Queue && path.queue && path.elevation && path.elevation->slope == save::kSlopeOnGround;
    }

    namespace
    {

        /// Where the picture is read at tile point (u, v) when the cell is turned clockwise `turns` times.
        glm::vec2 turned(float u, float v, std::uint8_t turns)
        {
            for (std::uint8_t i = 0; i < turns % 4; ++i)
            {
                const float was = u;
                u = v;
                v = 1 - was;
            }
            return { u, v };
        }

        glm::vec2 queue_uv(QueueCell cell, glm::vec2 at)
        {
            constexpr float kInset = 1.0f / 256;
            const auto n = static_cast<int>(cell);
            const glm::vec2 origin { (n & 1) != 0 ? 0.5f : 0.0f, (n & 2) != 0 ? 0.5f : 0.0f };
            return origin + glm::vec2(kInset) + at * (0.5f - 2 * kInset);
        }

    } // namespace

    GroundQueues ground_queues(std::span<const save::PathTile> paths, const save::TerrainGrid& grid)
    {
        GroundQueues out;
        std::map<std::size_t, std::vector<GroundVertex>> by_painting;
        bool any = false;
        for (const save::PathTile& p : paths)
        {
            if (!is_ground_queue(p) || p.surface_name.empty() || !grid.contains(p.col, p.row))
                continue;
            const QueuePainting painting { p.surface_name, p.surface_colours.value_or(std::array<std::int32_t, 3> { }) };
            auto found = std::ranges::find(out.paintings, painting);
            if (found == out.paintings.end())
            {
                if (out.paintings.size() > static_cast<std::size_t>(std::numeric_limits<std::uint8_t>::max()))
                    continue;
                out.paintings.push_back(painting);
                found = std::prev(out.paintings.end());
            }

            const QueueShape shape = queue_shape(p.queue->start_direction, p.queue->end_direction);
            const save::TerrainCell& cell = grid.at(p.col, p.row);
            std::array<GroundVertex, 4> q;
            for (int c = 0; c < 4; ++c)
            {
                const auto cu = static_cast<std::size_t>(c);
                const glm::vec3 at { grid.world_x(p.col + c % 2), cell.corners[cu] + kPathLift, grid.world_z(p.row + c / 2) };
                q[cu] = { at, glm::vec4(1.0f), queue_uv(shape.cell, turned(static_cast<float>(c % 2), static_cast<float>(c / 2), shape.turns)), ground_normal(grid, p.col, p.row, c) };
            }
            auto& vertices = by_painting[static_cast<std::size_t>(found - out.paintings.begin())];
            for (const int k : { 0, 1, 2, 3, 2, 1 })
                vertices.push_back(q[static_cast<std::size_t>(k)]);

            const glm::vec3 low { q[0].position.x, cell.min_height(), q[0].position.z };
            const glm::vec3 high { q[3].position.x, cell.max_height() + kPathLift, q[3].position.z };
            out.mesh.low = any ? glm::min(out.mesh.low, low) : low;
            out.mesh.high = any ? glm::max(out.mesh.high, high) : high;
            any = true;
        }

        for (auto& [index, vertices] : by_painting)
        {
            GroundBatch batch { GroundPaint::Queue, static_cast<std::uint8_t>(index), static_cast<std::uint32_t>(out.mesh.indices.size()), static_cast<std::uint32_t>(vertices.size()) };
            for (const GroundVertex& v : vertices)
            {
                out.mesh.indices.push_back(static_cast<std::uint32_t>(out.mesh.vertices.size()));
                out.mesh.vertices.push_back(v);
            }
            out.mesh.batches.push_back(batch);
        }
        return out;
    }

} // namespace rct3::park
