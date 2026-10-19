// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/ground_shaping.hpp"

#include "park/footpath_builder.hpp"
#include "park/ride_placement.hpp"
#include "scene/park_footpaths.hpp"
#include "scene/park_scene.hpp"
#include "scene/ride_placing.hpp"

#include <cmath>

namespace rct3::scene
{

    namespace
    {
        // Price, in hundredths, for each step of one tile.
        constexpr std::int32_t kStepCost = 100;

        /// Takes the held tile one step up or down; false when it may not go.
        bool step(ParkScene& scene, bool up)
        {
            GroundShaping& s = scene.shaping;
            if (scene.park.cash && *scene.park.cash < kStepCost)
                return false;
            const park::RideSite site { s.tile->col, s.tile->row, 0, park::shaped_height(scene.grid.at(s.tile->col, s.tile->row), up), { 1, 1 } };
            if (park::site_problem(site, scene.grid, scene.footpaths.paths, s.solids) != park::SiteProblem::None)
                return false;
            park::level_ground(scene.grid, site);
            if (scene.park.cash)
                *scene.park.cash -= kStepCost;
            s.steps += up ? 1 : -1;
            return true;
        }
    } // namespace

    void toggle_ground_shaping(ParkScene& scene)
    {
        const bool on = !scene.shaping.on;
        stop_ground_shaping(scene);
        scene.shaping.on = on;
    }

    void stop_ground_shaping(ParkScene& scene)
    {
        scene.shaping = { };
    }

    bool ground_shaping(const ParkScene& scene)
    {
        return scene.shaping.on;
    }

    void update_ground_shaping(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool down, bool over)
    {
        GroundShaping& s = scene.shaping;
        if (!s.on || !down)
        {
            s.tile.reset();
            return;
        }
        if (!s.tile)
        {
            s.tile = over ? std::nullopt : ground_tile(scene, at, size);
            if (!s.tile)
                return;
            s.press_y = at.y;
            s.steps = 0;
            // The ground under a stroke cannot reach things standing on it, so they are only gathered once.
            s.solids = park::solid_tiles(scene.objects, scene.models, scene.grid);
        }
        const auto wanted = static_cast<int>(std::lround((s.press_y - at.y) / park::kSlopeDragStep));
        bool moved = false;
        while (s.steps != wanted && step(scene, wanted > s.steps))
            moved = true;
        if (moved)
            rebuild_ground(device, scene, assets);
    }

} // namespace rct3::scene
