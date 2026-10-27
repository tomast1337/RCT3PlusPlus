// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/enclosure_tools.hpp"

#include "park/boundary_fence.hpp"
#include "park/enclosures.hpp"
#include "park/item_category.hpp"
#include "park/support_columns.hpp"
#include "park/tile_shaping.hpp"
#include "scene/park_scene.hpp"
#include "scene/park_setup.hpp"
#include "scene/park_view.hpp"
#include "scene/ride_placing.hpp"
#include "scene/track_building.hpp"
#include "scene/zoo_animals.hpp"
#include "simulation/animals.hpp"
#include "simulation/components.hpp"
#include "simulation/systems.hpp"
#include "strings/unicode.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <future>
#include <glm/gtc/matrix_transform.hpp>
#include <utility>

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

        /// What a length of fence `fence` costs and pays back, as the enclosure list prices it; nothing until
        /// the list is read.
        park::FencePrice fence_price(const ParkScene& scene, std::string_view fence)
        {
            const auto& items = scene.build.items;
            if (items.valid() && items.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                for (const park::BuildItem& i : items.get())
                    if (i.kind == park::BuildKind::EnclosureFence && i.symbol == fence)
                        return { .build = i.price, .refund = i.refund };
            return { };
        }

        /// Whether an item of `category` keeps enclosures off the tiles under it: rides, shops, park entrances
        /// and the floors and roofs of buildings do; plants, scenery, fences and what stands on paths do not.
        bool keeps_out(std::uint32_t category) noexcept
        {
            using park::ItemCategory;
            return std::ranges::contains(std::array { ItemCategory::TrackPiece, ItemCategory::ParkEntrance, ItemCategory::Ride, ItemCategory::RideEntrance, ItemCategory::RideExit, ItemCategory::Stall, ItemCategory::Floor, ItemCategory::Roof }, category, [](ItemCategory c) { return std::to_underlying(c); });
        }

        /// The tiles a drag may not take into enclosures, or with `deleting`, out of them, row by row. Into:
        /// land the park does not own outright, paths, and what keeps enclosures out. Out of: what keeps
        /// enclosures out, animal houses and the animals.
        /// shortcut: lakes and the rest of an item over several tiles are not kept whole; keep them so once
        /// enclosures may hold water.
        std::vector<bool> kept_tiles(const ParkScene& scene, bool deleting)
        {
            const save::TerrainGrid& g = scene.grid;
            std::vector<bool> kept(static_cast<std::size_t>(g.width) * static_cast<std::size_t>(g.height));
            const auto keep = [&](std::int32_t col, std::int32_t row) {
                if (g.contains(col, row))
                    kept[static_cast<std::size_t>(row * g.width + col)] = true;
            };
            const auto keep_at = [&](float x, float z) { keep(static_cast<std::int32_t>(std::floor((x - g.origin_x) / g.tile_size_x)), static_cast<std::int32_t>(std::floor((z - g.origin_z) / g.tile_size_z))); };
            for (const save::PlacedObject& o : scene.objects)
            {
                const auto model = scene.models.find(park::model_key(o));
                if (o.hidden || o.fixture || model == scene.models.end() || !keeps_out(model->second.category))
                    continue;
                park::SupportFootprint shape;
                shape.x_squares = model->second.squares[0];
                shape.z_squares = model->second.squares[1];
                for (const auto& [col, row] : park::footprint_tiles(o, shape))
                    keep(col, row);
            }
            const auto& r = scene.sim.registry;
            if (deleting)
            {
                for (const auto [e, house] : r.view<const simulation::AnimalHouse>().each())
                    for (float z = house.low.y + g.tile_size_z / 2; z < house.high.y; z += g.tile_size_z)
                        for (float x = house.low.x + g.tile_size_x / 2; x < house.high.x; x += g.tile_size_x)
                            keep_at(x, z);
                for (const auto [e, animal, pose] : r.view<const simulation::Animal, const simulation::Pose>().each())
                    keep_at(pose.position.x, pose.position.z);
                return kept;
            }
            for (std::int32_t row = 0; row < g.height; ++row)
                for (std::int32_t col = 0; col < g.width; ++col)
                    if (!park::owned_outright(g.at(col, row)))
                        keep(col, row);
            for (const save::PathTile& p : scene.footpaths.paths)
                keep(p.col, p.row);
            return kept;
        }

        /// What letting go of the drag now would do, worked out again only once the area marked changes.
        const EnclosureTool::Marked& marked_now(ParkScene& scene)
        {
            EnclosureTool& t = scene.enclosing;
            const auto area = marked(*t.corner, *t.other);
            if (t.marked && t.marked->area == area)
                return *t.marked;
            save::EnclosureMap map = scene.enclosures;
            const auto free = [&](save::Tile at) { return scene.grid.contains(at.col, at.row) && !t.kept[static_cast<std::size_t>(at.row * scene.grid.width + at.col)]; };
            if (deleting(t))
                park::unenclose(map, *t.corner, *t.other, free);
            else
                (void)park::enclose(map, scene.grid, *t.corner, *t.other, t.item->symbol, free);
            const auto price = [&](std::string_view fence) { return fence_price(scene, fence); };
            const std::int64_t cost = park::fence_cost(scene.enclosures, map, price);
            return t.marked.emplace(EnclosureTool::Marked { .area = area, .enclosures = std::move(map), .cost = cost });
        }

        /// Whether the park can pay `cost`.
        bool affordable(const ParkScene& scene, std::int64_t cost)
        {
            return !scene.park.cash || *scene.park.cash >= cost;
        }

        /// Whether the animal picked may come down on `tile`: on the park's own land, and paid for. Outside every
        /// enclosure it gets out as its crate opens.
        bool crate_allowed(const ParkScene& scene, save::Tile tile)
        {
            return scene.grid.contains(tile.col, tile.row) && park::owned_outright(scene.grid.at(tile.col, tile.row)) && affordable(scene, scene.enclosing.item->price);
        }

        /// The animal of `item` in its crate on `tile`, paid for.
        void put_animal(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, const park::BuildItem& item, save::Tile tile)
        {
            const glm::vec3 at = tile_point(scene.grid, tile);
            const std::string name = strings::to_utf8(assets.strings->get(item.name_text));
            // shortcut: every animal bought is a grown one; offer young ones once the animal list shows them.
            (void)simulation::add_animal(scene.sim, { .species = item.symbol, .overlay = item.overlay, .name = name, .male = random::below(scene.looks, 2) == 0, .enclosure = scene.enclosures.at(tile.col, tile.row) }, at);
            if (scene.park.cash)
                *scene.park.cash -= item.price;
            float_money(scene, at, -item.price);
            (void)load_object_model(scene, scene.placing.models, assets, animal_crate());
            animals_changed(device, scene);
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
            {
                t.corner = tile;
                if (!animal)
                    t.kept = kept_tiles(scene, deleting(t));
            }
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
            // Let go: the tiles go in or out, the fence put up paid for and the fence taken down paid back.
            const EnclosureTool::Marked& m = marked_now(scene);
            if (affordable(scene, m.cost) && m.enclosures.tiles != scene.enclosures.tiles)
            {
                scene.enclosures = m.enclosures;
                if (scene.park.cash)
                    *scene.park.cash -= m.cost;
                float_money(scene, tile_point(scene.grid, *t.other), -m.cost);
                enclosures_changed(scene, assets);
            }
            t.corner.reset();
            t.other.reset();
            t.marked.reset();
            t.kept.clear();
        }
        // The marks: the tiles letting go would take in or out, red when the park cannot pay; else the tile
        // under the pointer.
        std::optional<std::array<std::int32_t, 4>> marks;
        if (animal && (t.other || tile))
        {
            const save::Tile spot = t.other ? *t.other : *tile;
            marks = marked(spot, spot);
        }
        else if (t.corner && t.other)
            marks = marked(*t.corner, *t.other);
        else if (tile)
            marks = marked(*tile, *tile);
        const bool dragging = !animal && t.corner && t.other;
        const bool moved = dragging && (!t.marked || t.marked->area != *marks);
        if (marks != t.drawn || moved)
        {
            park::TerrainMesh mesh;
            if (animal && marks)
                park::tile_marks(mesh, scene.grid, (*marks)[0], (*marks)[1], (*marks)[2], (*marks)[3], park::TilePart::Whole, crate_allowed(scene, { (*marks)[0], (*marks)[1] }) ? kMarkColour : kRefusedColour);
            else if (dragging)
            {
                const EnclosureTool::Marked& m = marked_now(scene);
                const glm::vec4 colour = affordable(scene, m.cost) ? kMarkColour : kRefusedColour;
                for (std::int32_t row = m.area[1]; row <= m.area[3]; ++row)
                    for (std::int32_t col = m.area[0]; col <= m.area[2]; ++col)
                        if ((m.enclosures.at(col, row) >= 0) != (scene.enclosures.at(col, row) >= 0))
                            park::tile_marks(mesh, scene.grid, col, row, col, row, park::TilePart::Whole, colour);
            }
            else if (marks)
                park::tile_marks(mesh, scene.grid, (*marks)[0], (*marks)[1], (*marks)[2], (*marks)[3], park::TilePart::Whole, kMarkColour);
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
        // Enclosures joined or cut in two may have moved to other places in the map: the houses and dung take
        // the place of the ground under them now. The animals do so as they next move.
        const save::TerrainGrid& g = scene.grid;
        const auto under = [&](float x, float z) { return scene.enclosures.at(static_cast<std::int32_t>(std::floor((x - g.origin_x) / g.tile_size_x)), static_cast<std::int32_t>(std::floor((z - g.origin_z) / g.tile_size_z))); };
        auto& r = scene.sim.registry;
        for (const auto [e, house] : r.view<simulation::AnimalHouse>().each())
            house.enclosure = under((house.low.x + house.high.x) / 2, (house.low.y + house.high.y) / 2);
        for (const auto [e, dung, pose] : r.view<simulation::Dung, const simulation::Pose>().each())
            dung.enclosure = under(pose.position.x, pose.position.z);
    }

} // namespace rct3::scene
