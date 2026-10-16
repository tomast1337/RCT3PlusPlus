// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park map window in a played park: what it draws, gathered when the park loads, its pictures on the
// device, and the window itself, opened and closed by the tool row's Map button. The ground's picture is
// drawn the first time the window opens, so parks load no slower for it.
//
// The park scene only opens the window, hands it the camera and the people each frame, and moves its camera
// to wherever the player clicks or drags on the map.
#pragma once

#include "config/game_install.hpp"
#include "park/attraction_objects.hpp"
#include "park/camera.hpp"
#include "park/ground_textures.hpp"
#include "park/object_mesh.hpp"
#include "park/park_map.hpp"
#include "render/device.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"
#include "ui/desktop.hpp"
#include "ui/string_table.hpp"

#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace rct3::scene
{

    struct ParkMap
    {
        park::MapFeatures features;
        std::optional<park::MapGround> ground; ///< Drawn when the window first opens
        park::MapLayers layers = park::all_map_layers();
        render::TextureHandle picture;
        render::TextureHandle people;
        bool stale = true;         ///< The picture no longer shows the layers picked
        float since_people = 1e9f; ///< Seconds since the people's picture was drawn
    };

    /// What a park's map shows: its paths, water, scenery and attractions by kind, and the ground in the
    /// colours of its surfaces' pictures in `images`. The kinds of stalls and facilities are read from the
    /// overlays that hold them, under `install`.
    [[nodiscard]] ParkMap park_map(const save::TerrainGrid& grid, std::span<const save::PathTile> paths, const save::WaterLayer& water, std::span<const save::PlacedObject> objects, const park::ObjectModels& models, const park::Fitted& fitted, const park::AttractionObjects& attractions, const save::RideModel& rides, const park::GroundImages& images, const config::GameInstall& install);

    /// Opens the map window, or closes it when it is open.
    void toggle_park_map(ParkMap& map, ui::Desktop& desktop, const ui::StringTable& strings);

    /// Brings the open window up to date: its pictures, the people (`people` is asked only when they are
    /// drawn again) and the camera, seen in a view `aspect` times as wide as it is tall. Returns the park
    /// point (x, z) the player picked on the map since the last frame, if any.
    [[nodiscard]] std::optional<glm::vec2> update_park_map(render::Device& device, ParkMap& map, ui::Desktop& desktop, const save::TerrainGrid& grid, const park::ParkCamera& camera, float aspect, const std::function<std::vector<park::MapPerson>()>& people, float elapsed);

    void destroy_park_map(render::Device& device, ParkMap& map);

} // namespace rct3::scene
