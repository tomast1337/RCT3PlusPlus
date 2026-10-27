// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the view menu adds over the park: the tile grid and contours on the ground, and a label of the height
// over each path or piece of track near the middle of the view. The labels stay on until switched off, while
// building or just watching the park.
//
// Money spent or paid back floats up from where it was, unless the view menu hides it; what guests pay at
// rides and stalls floats up from their entrances when the view menu asks for that too.
#pragma once

#include "park/terrain_mesh.hpp"
#include "render/device.hpp"
#include "save/model/world.hpp"
#include "ui/money_text.hpp"
#include "ui/widgets/money_floaters.hpp"
#include "ui/widgets/world_labels.hpp"

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace rct3::scene
{

    struct ParkScene;
    struct ViewLines;

    /// Makes the grid and contours the view menu shows, and drops those it hides.
    void update_view_lines(render::Device& device, ParkScene& scene);

    /// Puts `mesh` in the lines' buffers, in place of what they held.
    void set_view_lines(render::Device& device, ViewLines& lines, const park::TerrainMesh& mesh);

    /// Draws the grid, the contours and the land tools' marks over the ground.
    void draw_view_lines(render::Device& device, const ParkScene& scene, const glm::mat4& transform, const render::SceneLight& light);

    /// Drops the lines' buffers.
    void drop_view_lines(render::Device& device, ParkScene& scene);

    /// The height labels the view menu shows, on a view `size` across (interface units), farthest first.
    [[nodiscard]] std::vector<ui::WorldLabel> height_labels(const ParkScene& scene, ui::Point size);

    /// The ground in the middle of `tile`.
    [[nodiscard]] glm::vec3 tile_point(const save::TerrainGrid& grid, save::Tile tile);

    /// Floats `amount` (hundredths, above 0 for money in) up from `at`. One begun near there a moment ago takes
    /// it instead, so a drag or a held brush shows a few sums rather than a crowd. Nothing floats for a zero
    /// amount unless `zero`, as scenery pulled down shows what it cost even when that is nothing.
    void float_money(ParkScene& scene, glm::vec3 at, std::int64_t amount, bool zero = false);

    /// Floats up what guests paid since the last call, when shown; ages the floats and drops those that ended.
    /// Returns them pinned on a view `size` across (interface units).
    [[nodiscard]] std::vector<ui::MoneyFloat> money_floats(ParkScene& scene, ui::Point size, const ui::Currency& currency, float elapsed);

} // namespace rct3::scene
