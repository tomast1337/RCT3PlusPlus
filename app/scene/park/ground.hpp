// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park scene's ground built again from its grid with what lies on it: the paths and queues laid on the
// ground, the raised paths' decks, and what the rides and stalls pave, with the pictures the path styles and
// queues paint on it. Laying paths, changing the land and placing rides all build the ground again this way.
#pragma once

#include "park/ground/queues.hpp"
#include "park/terrain_mesh.hpp"
#include "render/device.hpp"

#include <optional>
#include <string>
#include <vector>

namespace rct3::ui
{
    struct InterfaceAssets;
}

namespace rct3::scene::park
{

    struct ParkScene;

    /// Builds the ground again from the scene's grid, with the paths and what the rides and stalls pave as they are now;
    /// what changed lies on the tiles of `touched`, or anywhere without it.
    void rebuild_ground(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, std::optional<rct3::park::TileRect> touched = std::nullopt);

    /// Works out what the rides and stalls pave again from the scene's attractions and the models of what they
    /// are placed as, and marks the ground for building again.
    void refresh_pavings(ParkScene& scene);

    /// Has the scene's ground pictures match `styles`, the path styles in the order the ground's batches
    /// number them, loading those it lacks.
    void picture_styles(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, const std::vector<std::string>& styles);

    /// The same for the ground queues' `paintings`.
    void picture_queues(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, const std::vector<rct3::park::ground::QueuePainting>& paintings);

} // namespace rct3::scene::park
