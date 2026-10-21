// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/mountain_mesh.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace rct3::park
{

    std::string_view background_theme(std::optional<std::int32_t> stored)
    {
        if (stored && *stored >= 0 && static_cast<std::size_t>(*stored) < kBackgroundThemes.size())
            return kBackgroundThemes[static_cast<std::size_t>(*stored)];
        return kBackgroundThemes[kDefaultBackground];
    }

    std::array<std::string, 2> mountain_pictures(std::string_view theme)
    {
        const std::string base = std::format("Background{}", theme);
        return { std::format("{}1", base), std::format("{}2", base) };
    }

    MountainPlace mountain_place(const save::TerrainGrid& grid, std::string_view theme)
    {
        const auto at = std::ranges::find(kBackgroundThemes, theme);
        const float drop = at != kBackgroundThemes.end() ? kMountainDrop[static_cast<std::size_t>(at - kBackgroundThemes.begin())] : 0.0f;
        return { { (grid.world_x(0) + grid.world_x(grid.width)) / 2, -drop, (grid.world_z(0) + grid.world_z(grid.height)) / 2 }, kMountainScale };
    }

    ObjectMesh mountain_mesh(const MountainShapes& shapes, const MountainPlace& place)
    {
        ObjectMesh mesh;
        const float scale = place.scale;
        for (const SkyShape& shape : shapes)
        {
            if (shape.vertices.empty() || shape.indices.empty())
                continue;
            const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
            for (const ModelVertex& v : shape.vertices)
                mesh.vertices.push_back({ place.centre + v.position * scale, glm::vec4(1.0f), v.uv });
            ObjectBatch batch;
            batch.texture = shape.texture;
            batch.picture = shape.texture;
            batch.blend = PartBlend::Cutout;
            batch.first = static_cast<std::uint32_t>(mesh.indices.size());
            for (const std::uint32_t i : shape.indices)
            {
                if (i < shape.vertices.size())
                    mesh.indices.push_back(base + i);
            }
            batch.count = static_cast<std::uint32_t>(mesh.indices.size()) - batch.first;
            mesh.batches.push_back(std::move(batch));
        }
        mesh.placed = mesh.batches.empty() ? 0 : 1;
        return mesh;
    }

} // namespace rct3::park
