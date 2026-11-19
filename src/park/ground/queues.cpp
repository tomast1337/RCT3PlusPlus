// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground/queues.hpp"

#include "core/types.hpp"
#include "park/ground/paths.hpp"
#include "park/path/decks.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>

namespace rct3::park::ground
{

    QueueShape queue_shape(u8 start, u8 end) noexcept
    {
        const auto in = path::stored_heading(start);
        const auto out = path::stored_heading(end);
        if (!in || !out)
            return { };
        // Counted as the picture turns, from its top: the row before, then the next column, the next row, the
        // column before, which runs through Side's numbers backwards.
        const i32 a = Side::kRowBefore.turned(-in->value).value;
        const i32 b = Side::kRowBefore.turned(-out->value).value;
        const auto turns = static_cast<u8>(a);
        if (a == b || (a + 2) % 4 == b)
            return { QueueCell::Straight, static_cast<u8>(a % 2) };
        // Turned so the back side faces where the line comes in; it goes out one side or the other.
        return { (a + 3) % 4 == b ? QueueCell::TurnLeftSide : QueueCell::TurnRightSide, turns };
    }

    bool is_ground_queue(const save::model::PathTile& path) noexcept
    {
        return path.kind == save::model::PathKind::Queue && path.queue && path.elevation && path.elevation->slope == save::model::kSlopeOnGround;
    }

    namespace
    {

        /// Where the picture is read at tile point (u, v) when the cell is turned clockwise `turns` times.
        glm::vec2 turned(f32 u, f32 v, u8 turns)
        {
            for (u8 i = 0; i < turns % 4; ++i)
            {
                const f32 was = u;
                u = v;
                v = 1 - was;
            }
            return { u, v };
        }

        glm::vec2 queue_uv(QueueCell cell, glm::vec2 at)
        {
            constexpr f32 kInset = 1.0f / 256;
            const auto n = static_cast<i32>(cell);
            const glm::vec2 origin { (n & 1) != 0 ? 0.5f : 0.0f, (n & 2) != 0 ? 0.5f : 0.0f };
            return origin + glm::vec2(kInset) + at * (0.5f - 2 * kInset);
        }

    } // namespace

    GroundQueues ground_queues(std::span<const save::model::PathTile> paths, const save::model::TerrainGrid& grid, std::optional<TileRect> area)
    {
        GroundQueues out;
        std::map<usize, std::vector<GroundVertex>> by_painting;
        bool any = false;
        for (const save::model::PathTile& p : paths)
        {
            if (!is_ground_queue(p) || p.surface_name.empty() || !grid.contains(p.col, p.row))
                continue;
            const QueuePainting painting { p.surface_name, p.surface_colours.value_or(std::array<i32, 3> { }) };
            auto found = std::ranges::find(out.paintings, painting);
            if (found == out.paintings.end())
            {
                if (out.paintings.size() > static_cast<usize>(std::numeric_limits<u8>::max()))
                    continue;
                out.paintings.push_back(painting);
                found = std::prev(out.paintings.end());
            }
            if (area && !area->contains(p.col, p.row))
                continue;

            const QueueShape shape = queue_shape(p.queue->start_direction, p.queue->end_direction);
            const save::model::TerrainCell& cell = grid.at(p.col, p.row);
            std::array<GroundVertex, 4> q;
            for (i32 c = 0; c < 4; ++c)
            {
                const auto cu = static_cast<usize>(c);
                const glm::vec3 at { grid.world_x(p.col + c % 2), cell.corners[cu] + kPathLift, grid.world_z(p.row + c / 2) };
                q[cu] = { at, glm::vec4(1.0f), queue_uv(shape.cell, turned(static_cast<f32>(c % 2), static_cast<f32>(c / 2), shape.turns)), ground_normal(grid, { p.col, p.row }, c) };
            }
            auto& vertices = by_painting[static_cast<usize>(found - out.paintings.begin())];
            for (const i32 k : { 0, 1, 2, 3, 2, 1 })
                vertices.push_back(q[static_cast<usize>(k)]);

            const glm::vec3 low { q[0].position.x, cell.min_height(), q[0].position.z };
            const glm::vec3 high { q[3].position.x, cell.max_height() + kPathLift, q[3].position.z };
            out.mesh.low = any ? glm::min(out.mesh.low, low) : low;
            out.mesh.high = any ? glm::max(out.mesh.high, high) : high;
            any = true;
        }

        for (auto& [index, vertices] : by_painting)
        {
            GroundBatch batch { GroundPaint::Queue, static_cast<u8>(index), static_cast<u32>(out.mesh.indices.size()), static_cast<u32>(vertices.size()) };
            for (const GroundVertex& v : vertices)
            {
                out.mesh.indices.push_back(static_cast<u32>(out.mesh.vertices.size()));
                out.mesh.vertices.push_back(v);
            }
            out.mesh.batches.push_back(batch);
        }
        return out;
    }

} // namespace rct3::park::ground
