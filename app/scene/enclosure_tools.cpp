// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/enclosure_tools.hpp"

#include "park/boundary_fence.hpp"
#include "park/enclosures.hpp"
#include "park/tile_shaping.hpp"
#include "scene/park_scene.hpp"
#include "scene/park_view.hpp"
#include "scene/ride_placing.hpp"
#include "scene/track_building.hpp"

#include <algorithm>
#include <array>

namespace rct3::scene
{

    namespace
    {
        constexpr glm::vec4 kMarkColour { 0.45f, 0.9f, 0.4f, 0.9f };
        constexpr glm::vec4 kRefusedColour { 1.0f, 0.35f, 0.3f, 0.9f };

        bool deleting(const EnclosureTool& t)
        {
            return t.fence && t.fence->symbol == park::kEnclosureDelete;
        }

        /// The area marked, as its least and greatest column and row.
        std::array<std::int32_t, 4> marked(save::Tile a, save::Tile b)
        {
            return { std::min(a.col, b.col), std::min(a.row, b.row), std::max(a.col, b.col), std::max(a.row, b.row) };
        }

        /// The enclosures with the area marked put in, or taken out; and what the new fence costs.
        std::pair<save::EnclosureMap, std::int64_t> marked_enclosures(const ParkScene& scene, save::Tile a, save::Tile b)
        {
            const EnclosureTool& t = scene.enclosing;
            save::EnclosureMap map = scene.enclosures;
            if (deleting(t))
            {
                // shortcut: fence taken down is not paid back; pay the fence's refund once enclosures record their panels.
                park::unenclose(map, a, b);
                return { std::move(map), 0 };
            }
            park::enclose(map, scene.grid, a, b, t.fence->symbol);
            const auto panels = [&](const save::EnclosureMap& m) { return static_cast<std::int64_t>(park::enclosure_fences(m, scene.grid, 0).pieces.size()); };
            return { std::move(map), std::max<std::int64_t>(0, panels(map) - panels(scene.enclosures)) * t.fence->price };
        }

        /// Whether the area may be worked on: all of it land the park owns outright, and the fence paid for.
        bool allowed(const ParkScene& scene, const std::array<std::int32_t, 4>& area, std::int64_t cost)
        {
            for (std::int32_t row = area[1]; row <= area[3]; ++row)
                for (std::int32_t col = area[0]; col <= area[2]; ++col)
                    if (!scene.grid.contains(col, row) || !park::owned_outright(scene.grid.at(col, row)))
                        return false;
            return !scene.park.cash || *scene.park.cash >= cost;
        }
    } // namespace

    bool start_enclosing(ParkScene& scene, const park::BuildItem& item)
    {
        if (item.kind != park::BuildKind::EnclosureFence)
            return false;
        scene.enclosing.fence = item;
        scene.enclosing.corner.reset();
        scene.enclosing.other.reset();
        return true;
    }

    void stop_enclosing(ParkScene& scene)
    {
        scene.enclosing = EnclosureTool { .drawn = scene.enclosing.drawn }; // The marks go on the next frame
    }

    void update_enclosing(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, ui::Point at, ui::Point size, bool down, bool over)
    {
        EnclosureTool& t = scene.enclosing;
        const auto tile = t.fence && !over ? ground_tile(scene, at, size) : std::nullopt;
        if (t.fence && down && (t.corner || tile))
        {
            if (!t.corner)
                t.corner = tile;
            if (tile)
                t.other = tile;
        }
        else if (t.corner && t.other)
        {
            // Let go: the area goes in or out.
            const auto [map, cost] = marked_enclosures(scene, *t.corner, *t.other);
            if (allowed(scene, marked(*t.corner, *t.other), cost))
            {
                scene.enclosures = map;
                if (scene.park.cash)
                    *scene.park.cash -= cost;
                if (cost > 0)
                    float_money(scene, tile_point(scene.grid, *t.other), -cost);
                enclosures_changed(scene, assets);
            }
            t.corner.reset();
            t.other.reset();
        }
        std::optional<std::array<std::int32_t, 4>> marks;
        bool ok = true;
        if (t.corner && t.other)
        {
            marks = marked(*t.corner, *t.other);
            ok = allowed(scene, *marks, marked_enclosures(scene, *t.corner, *t.other).second);
        }
        else if (tile)
            marks = marked(*tile, *tile);
        if (marks != t.drawn)
        {
            park::TerrainMesh mesh;
            if (marks)
                park::tile_marks(mesh, scene.grid, (*marks)[0], (*marks)[1], (*marks)[2], (*marks)[3], park::TilePart::Whole, ok ? kMarkColour : kRefusedColour);
            set_view_lines(device, scene.enclosure_marks, mesh);
            t.drawn = marks;
        }
    }

    void enclosures_changed(ParkScene& scene, const ui::InterfaceAssets& assets)
    {
        wait_for_build(scene);
        for (save::PlacedObject& o : scene.objects)
            if (std::ranges::contains(scene.enclosure_panels, o.id))
                o.hidden = true;
        scene.enclosure_panels.clear();
        park::BoundaryFence fence = park::enclosure_fences(scene.enclosures, scene.grid, last_id(scene));
        for (save::PlacedObject& panel : fence.pieces)
        {
            if (!load_object_model(scene, scene.placing.models, assets, panel))
                continue;
            panel.colours = scene.models.at(park::model_key(panel)).default_colours;
            scene.enclosure_panels.push_back(panel.id);
            scene.objects.push_back(std::move(panel));
        }
        scene.leans.merge(fence.leans);
        objects_changed(scene);
    }

} // namespace rct3::scene
