// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/enclosure_tools.hpp"

#include "park/boundary_fence.hpp"
#include "park/enclosures.hpp"
#include "park/tile_shaping.hpp"
#include "scene/park_scene.hpp"
#include "scene/park_setup.hpp"
#include "scene/park_view.hpp"
#include "scene/ride_placing.hpp"
#include "scene/track_building.hpp"
#include "simulation/animals.hpp"
#include "simulation/components.hpp"
#include "simulation/systems.hpp"
#include "strings/unicode.hpp"

#include <algorithm>
#include <array>
#include <glm/gtc/matrix_transform.hpp>

namespace rct3::scene
{

    namespace
    {
        constexpr glm::vec4 kMarkColour { 0.45f, 0.9f, 0.4f, 0.9f };
        constexpr glm::vec4 kRefusedColour { 1.0f, 0.35f, 0.3f, 0.9f };

        bool deleting(const EnclosureTool& t)
        {
            return t.item && t.item->symbol == park::kEnclosureDelete;
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
            park::enclose(map, scene.grid, a, b, t.item->symbol);
            const auto panels = [&](const save::EnclosureMap& m) { return static_cast<std::int64_t>(park::enclosure_fences(m, scene.grid, 0).pieces.size()); };
            return { std::move(map), std::max<std::int64_t>(0, panels(map) - panels(scene.enclosures)) * t.item->price };
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

        /// Whether the animal picked may come down on `tile`: on the park's own land, and paid for. Outside every
        /// enclosure it gets out as its crate opens.
        bool crate_allowed(const ParkScene& scene, save::Tile tile)
        {
            return allowed(scene, marked(tile, tile), scene.enclosing.item->price);
        }

        /// The animal of `item` in its crate on `tile`, paid for.
        void put_animal(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, const park::BuildItem& item, save::Tile tile)
        {
            const glm::vec3 at = tile_point(scene.grid, tile);
            const std::string name = strings::to_utf8(assets.strings->get(item.name_text));
            (void)simulation::add_animal(scene.sim, { .species = item.symbol, .overlay = item.overlay, .name = name, .enclosure = scene.enclosures.at(tile.col, tile.row) }, at);
            if (scene.park.cash)
                *scene.park.cash -= item.price;
            float_money(scene, at, -item.price);
            animals_changed(device, scene, assets);
        }
    } // namespace

    bool start_enclosing(ParkScene& scene, const park::BuildItem& item)
    {
        if (item.kind != park::BuildKind::EnclosureFence && item.kind != park::BuildKind::Animal)
            return false;
        scene.enclosing.item = item;
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
        const bool animal = t.item && t.item->kind == park::BuildKind::Animal;
        const auto tile = t.item && !over ? ground_tile(scene, at, size) : std::nullopt;
        if (t.item && down && (t.corner || tile))
        {
            if (!t.corner)
                t.corner = tile;
            if (tile)
                t.other = tile;
        }
        else if (t.corner && t.other && animal)
        {
            // Let go: the animal comes down in its crate, and the tool stays out for the next.
            if (crate_allowed(scene, *t.other))
                put_animal(device, scene, assets, *t.item, *t.other);
            t.corner.reset();
            t.other.reset();
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
        if (animal && (t.other || tile))
        {
            const save::Tile spot = t.other ? *t.other : *tile;
            marks = marked(spot, spot);
            ok = crate_allowed(scene, spot);
        }
        else if (t.corner && t.other)
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

    void house_placed(ParkScene& scene, const ui::InterfaceAssets& assets, const save::PlacedObject& house, const park::BuildItem& item, const park::RideSite& site)
    {
        const std::int32_t enclosure = park::enclosure_under(scene.enclosures, { site.col, site.row }, site.span);
        // The hut goes on a tile beside the house in the same enclosure that nothing stands on, the side it
        // faces first.
        // shortcut: a tile is taken when an object's corner is on it; check the hut's footprint against solid
        // items if huts end up inside large ones.
        const auto free = [&](save::Tile t) { return scene.enclosures.at(t.col, t.row) == enclosure && std::ranges::none_of(scene.objects, [&](const save::PlacedObject& o) { return !o.hidden && !o.fixture && o.col == t.col && o.row == t.row; }); };
        std::optional<save::Tile> spot;
        for (std::int32_t side = 0; side < 4 && !spot; ++side)
        {
            const std::int32_t way = (side + site.turns) % 4; // 0 past the last row, then round
            const std::int32_t across = way % 2 == 0 ? site.span[0] : site.span[1];
            for (std::int32_t i = across / 2, n = 0; n < across && !spot; ++n, i = (i + 1) % across)
            {
                const save::Tile t = way == 0 ? save::Tile { site.col + i, site.row + site.span[1] } : way == 1 ? save::Tile { site.col + site.span[0], site.row + i } : way == 2 ? save::Tile { site.col + i, site.row - 1 } : save::Tile { site.col - 1, site.row + i };
                if (free(t))
                    spot = t;
            }
        }
        const save::Tile middle { site.col + site.span[0] / 2, site.row + site.span[1] / 2 };
        const save::TerrainGrid& g = scene.grid;
        const glm::vec2 low { g.world_x(site.col), g.world_z(site.row) };
        const glm::vec2 high = low + glm::vec2 { static_cast<float>(site.span[0]) * g.tile_size_x, static_cast<float>(site.span[1]) * g.tile_size_z };
        simulation::AnimalHouse home { .object = house.id, .enclosure = enclosure, .troughs = tile_point(g, middle), .low = low, .high = high };
        if (spot && !item.scenery.empty())
        {
            save::PlacedObject hut;
            hut.symbol = item.scenery;
            hut.overlay = item.overlay;
            hut.col = spot->col;
            hut.row = spot->row;
            hut.base_height = park::ride_site({ spot->col, spot->row }, { 1, 1 }, site.turns, scene.grid).height;
            hut.rotation = site.turns;
            hut.id = last_id(scene) + 1;
            hut.owner_ref = hut.id;
            if (load_object_model(scene, scene.placing.models, assets, hut))
            {
                hut.colours = scene.models.at(park::model_key(hut)).default_colours;
                home.hut = hut.id;
                scene.placed.insert(hut.id);
                scene.objects.push_back(std::move(hut));
                objects_changed(scene);
            }
        }
        (void)simulation::add_animal_house(scene.sim, home, tile_point(scene.grid, spot.value_or(middle)));
    }

    void animals_changed(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets)
    {
        // shortcut: every animal is drawn as the same closed crate, standing where the animal is; draw each
        // species' own model, moving as the animal does, once animal models can be read. This is the one place
        // the animals' shapes are made; place_animals moves them.
        constexpr std::string_view kCrate = "Crate01";
        constexpr std::string_view kCrates = "WildAnimals/Crates/Crates";
        save::PlacedObject crate;
        crate.symbol = kCrate;
        crate.overlay = kCrates;
        scene.animals_shown.clear();
        std::vector<save::PlacedObject> shapes;
        if (load_object_model(scene, scene.placing.models, assets, crate))
            for (const auto [e, animal] : scene.sim.registry.view<const simulation::Animal>().each())
            {
                scene.animals_shown.push_back(e);
                crate.id = shapes.size() + 1;
                shapes.push_back(crate);
            }
        scene.animal_meshes = park::moving_meshes(shapes, scene.models, scene.grid);
        add_textures(device, scene, scene.animal_meshes.mesh.batches);
        place_animals(scene);
    }

    void place_animals(ParkScene& scene)
    {
        const auto& r = scene.sim.registry;
        std::vector<glm::mat4> places;
        places.reserve(scene.animals_shown.size());
        for (const entt::entity e : scene.animals_shown)
        {
            const auto* pose = r.valid(e) ? r.try_get<simulation::Pose>(e) : nullptr;
            const simulation::Pose at = pose != nullptr ? simulation::between_steps(*pose, scene.sim_time / simulation::kTickSeconds) : simulation::Pose { };
            places.push_back(pose != nullptr ? glm::rotate(glm::translate(glm::mat4(1.0f), at.position), at.heading.value, glm::vec3(0.0f, 1.0f, 0.0f)) : glm::mat4(0.0f));
        }
        park::move_meshes(scene.animal_meshes, places);
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
