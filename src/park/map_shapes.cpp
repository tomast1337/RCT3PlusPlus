// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/map_shapes.hpp"

#include "park/object_pick.hpp"

#include <array>

namespace rct3::park
{

    std::vector<MapShape> map_shapes(std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid, const Fitted& fitted, const MapLayerOf& layer_of)
    {
        std::vector<MapShape> shapes;
        for (const save::PlacedObject& o : objects)
        {
            if (o.hidden)
                continue;
            const std::optional<MapLayer> layer = layer_of(o);
            if (!layer)
                continue;
            const std::vector<glm::vec3> box = object_corners(o, models, grid, fitted);
            if (box.size() == 8)
                shapes.push_back({ *layer, ground_corners(std::span<const glm::vec3, 8>(box.data(), 8)) });
            else if (grid.contains(o.col, o.row))
                shapes.push_back({ *layer, tile_corners(grid, { o.col, o.row }) });
        }
        return shapes;
    }

} // namespace rct3::park
