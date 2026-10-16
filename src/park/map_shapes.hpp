// SPDX-License-Identifier: GPL-3.0-or-later
//
// The placed objects as the map draws them: the ground each one stands on, in the layer it belongs to. An
// object with a model covers the ground under the box it is picked in, turned as it is placed; one without
// covers its tile.
#pragma once

#include "park/object_mesh.hpp"
#include "park/park_map.hpp"
#include "save/model/world.hpp"

#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace rct3::park
{

    /// The layer an object is drawn in, or nothing to leave it off the map.
    using MapLayerOf = std::function<std::optional<MapLayer>(const save::PlacedObject&)>;

    /// The shapes of the objects of `objects` that are not hidden and that `layer_of` puts on the map.
    [[nodiscard]] std::vector<MapShape> map_shapes(std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid, const Fitted& fitted, const MapLayerOf& layer_of);

} // namespace rct3::park
