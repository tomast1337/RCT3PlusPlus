// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/object/chunks.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <cstdint>

namespace rct3::park::object
{

    ChunkGrid ChunkGrid::of(i32 width, i32 height, i32 tiles)
    {
        return { .across = std::max(1, (width + tiles - 1) / tiles), .down = std::max(1, (height + tiles - 1) / tiles), .tiles = tiles };
    }

    usize ChunkGrid::count() const
    {
        return static_cast<usize>(across) * static_cast<usize>(down);
    }

    usize ChunkGrid::at(i32 col, i32 row) const
    {
        const i32 c = std::clamp(col / tiles, 0, across - 1);
        const i32 r = std::clamp(row / tiles, 0, down - 1);
        return static_cast<usize>(r) * static_cast<usize>(across) + static_cast<usize>(c);
    }

    std::vector<std::vector<u32>> chunk_members(std::span<const save::model::PlacedObject> objects, const ChunkGrid& grid)
    {
        std::vector<std::vector<u32>> members(grid.count());
        for (usize i = 0; i < objects.size(); ++i)
            members[grid.at(objects[i].col, objects[i].row)].push_back(static_cast<u32>(i));
        return members;
    }

    std::vector<bool> changed_chunks(const LevelPlan& now, const LevelPlan& shown, std::span<const std::vector<u32>> members)
    {
        const usize n = now.steady.size();
        const bool same_objects = shown.steady.size() == n && shown.finer.size() == n && shown.coarser.size() == n && shown.bands.size() == now.bands.size() && now.finer.size() == n && now.coarser.size() == n;
        std::vector<bool> changed(members.size(), !same_objects);
        if (!same_objects)
            return changed;
        for (usize c = 0; c < members.size(); ++c)
            changed[c] = std::ranges::any_of(members[c], [&](u32 i) { return now.steady[i] != shown.steady[i] || now.finer[i] != shown.finer[i] || now.coarser[i] != shown.coarser[i] || (i < now.bands.size() && now.bands[i] != shown.bands[i]); });
        return changed;
    }

    void ShapeBox::take(const glm::vec3& point)
    {
        low = empty ? point : glm::min(low, point);
        high = empty ? point : glm::max(high, point);
        empty = false;
    }

    void ShapeBox::take(const ObjectMesh& mesh)
    {
        for (const GroundVertex& v : mesh.vertices)
            take(v.position);
    }

    void ShapeBox::take(const Billboards& boards)
    {
        for (const BillboardStand& s : boards.stands)
        {
            take(s.base - glm::vec3(s.half_width, 0.0f, s.half_width));
            take(s.base + glm::vec3(s.half_width, s.height, s.half_width));
        }
    }

} // namespace rct3::park::object
