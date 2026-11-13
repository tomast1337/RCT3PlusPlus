// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park/deleting.hpp"

#include "logging/log.hpp"
#include "park/demolish_refund.hpp"
#include "park/edge_walls.hpp"
#include "scene/park/questions.hpp"
#include "scene/park/ride_camera.hpp"
#include "scene/park/scene.hpp"
#include "scene/park/setup.hpp"
#include "scene/park/view.hpp"
#include "simulation/components.hpp"
#include "simulation/import_park.hpp"
#include "simulation/systems.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace rct3::scene::park
{

    namespace
    {

        constexpr std::string_view kEntranceStays = "YouMayNotSellParkEntrance";
        constexpr std::string_view kNoMoney = "PlaceObject_NotEnoughMoney";

        /// Takes the prop `index` out of the park.
        void destroy_prop(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, std::size_t index)
        {
            if (index >= scene.objects.size())
                return;
            const auto wall = rct3::park::edge_wall(scene.objects[index], scene.models, scene.grid);
            change_object(device, scene, index, [](save::model::PlacedObject& o) { o.hidden = true; });
            // Guests stop using what stood on the path, and the park keeps no bin or sign for it.
            const std::uint64_t id = scene.objects[index].id;
            auto& r = scene.sim.registry;
            std::vector<entt::entity> gone;
            const auto sign = std::ranges::find(scene.signs, id, &save::model::SignState::object_ref);
            for (const auto [e, item] : r.view<const simulation::PathItem>().each())
                if (item.object == id)
                {
                    gone.push_back(e);
                    if (sign != scene.signs.end() && sign->kind == 1)
                        scene.sim.paths.set_no_entry(item.node, simulation::no_entry_side(scene.objects[index]), false);
                }
            for (const auto [e, bed] : r.view<const simulation::FlowerBed>().each())
                if (bed.object == id)
                    gone.push_back(e);
            r.destroy(gone.begin(), gone.end());
            std::erase_if(scene.bins, [&](const save::model::BinState& b) { return b.object_ref == id; });
            std::erase_if(scene.signs, [&](const save::model::SignState& s) { return s.object_ref == id; });
            if (wall)
                wall_changed(device, scene, assets, *wall, false);
        }

    } // namespace

    void stop_deleting(ParkScene& scene)
    {
        if (auto panel = scene.desktop->root().find(kDeleteAreaName))
            panel->close();
        scene.deleting = DeleteTools { .drawn = scene.deleting.drawn }; // The marks go on the next frame
    }

    std::optional<std::int64_t> delete_props(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, std::span<const std::size_t> indices, bool floated)
    {
        std::vector<std::size_t> going;
        std::int64_t cost = 0;
        bool entrance = false;
        for (const std::size_t i : indices)
        {
            const auto model = scene.models.find(rct3::park::object::model_ref(scene.objects.at(i)));
            if (model != scene.models.end() && model->second.category == std::to_underlying(rct3::park::ItemCategory::ParkEntrance))
                entrance = true;
            else
            {
                going.push_back(i);
                cost += model != scene.models.end() ? model->second.removal_cost : 0;
            }
        }
        if (entrance)
            scene.prizes.emplace_back(kEntranceStays);
        if (going.empty())
            return std::nullopt;
        // Deleting the park cannot pay for asks in a box, as the original does, rather than the park message.
        if (cost > 0 && scene.park.cash && *scene.park.cash < cost)
        {
            scene.prizes.emplace_back(kNoMoney);
            return std::nullopt;
        }
        if (!simulation::park::book(scene.park, scene.park_model.finance, scene.ledger_month, cost, simulation::park::LedgerLine::Landscaping, false))
            return std::nullopt;
        const save::model::PlacedObject& first = scene.objects[going.front()];
        if (floated)
            float_money(scene, tile_point(scene.grid, { first.col, first.row }), -cost, true);
        for (const std::size_t i : going)
            destroy_prop(device, scene, assets, i);
        return cost;
    }

    void delete_paths(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, std::span<const std::uint64_t> ids)
    {
        std::vector<std::pair<std::int32_t, std::int32_t>> tiles;
        for (const save::model::PathTile& p : scene.footpaths.paths)
            if (std::ranges::contains(ids, p.id))
                tiles.emplace_back(p.col, p.row);
        std::ranges::sort(tiles);
        std::vector<std::size_t> extras;
        for (std::size_t i = 0; i < scene.objects.size(); ++i)
        {
            const save::model::PlacedObject& o = scene.objects[i];
            if (o.hidden || !std::ranges::binary_search(tiles, std::pair { o.col, o.row }))
                continue;
            const auto model = scene.models.find(rct3::park::object::model_ref(o));
            if (model != scene.models.end() && model->second.category >= std::to_underlying(rct3::park::ItemCategory::LitterBin) && model->second.category <= std::to_underlying(rct3::park::ItemCategory::PathFeature))
                extras.push_back(i);
        }
        const std::int64_t paid = delete_props(device, scene, assets, extras, false).value_or(0);
        remove_footpaths(device, scene, assets, ids, -paid);
    }

    void demolish(ParkScene& scene, const ui::InterfaceAssets& assets, rct3::park::AttractionRef ref)
    {
        save::model::AttractionCommon* base = attraction_common(scene, ref);
        if (base == nullptr || base->demolished)
            return;
        const bool ride = ref.kind == rct3::park::AttractionRef::Kind::Ride;
        if (ride && scene.building.ride == ref.index)
            close_track_building(scene);
        stop_ride_placing(scene);
        wait_for_build(scene);
        // shortcut: a flat ride or a stall gives back its item's price by the tracked rides' rule; use what its
        // kind's data says it is worth once that is read.
        const auto main = base->spot ? std::ranges::find(scene.objects, base->spot->object, &save::model::PlacedObject::id) : scene.objects.end();
        const auto model = main != scene.objects.end() ? scene.models.find(rct3::park::object::model_ref(*main)) : scene.models.end();
        const bool tracked = ride && scene.rides.rides[ref.index].kind == save::model::RideKind::Tracked;
        const std::int64_t value = tracked ? track_cost(scene, ref.index, assets) : (model != scene.models.end() ? model->second.price : 0);
        const std::int64_t back = rct3::park::demolish_refund(value, (base->flags & save::model::AttractionCommon::kNeverOpened) == 0);
        std::set<std::uint64_t> parts;
        // Its fence, which the park built round it, goes with it.
        if (const auto fence = scene.ride_fence_pieces.find(base->id); ride && fence != scene.ride_fence_pieces.end())
        {
            parts.insert(fence->second.begin(), fence->second.end());
            scene.ride_fence_pieces.erase(fence);
        }
        for (const auto& [id, owner] : scene.attractions)
            if (owner == ref)
                parts.insert(id);
        if (ride && scene.rides.rides[ref.index].track)
            for (const save::model::TrackPiece& piece : scene.rides.rides[ref.index].track->pieces)
                if (piece.platform)
                    parts.insert(piece.platform->id);
        std::optional<glm::vec3> at;
        for (std::size_t i = 0; i < scene.objects.size(); ++i)
        {
            save::model::PlacedObject& o = scene.objects[i];
            if (o.hidden || !(parts.contains(o.id) || parts.contains(o.owner_ref)))
                continue;
            o.hidden = true;
            at = at.value_or(tile_point(scene.grid, { o.col, o.row }));
            std::erase_if(scene.boned_rides, [&](const ParkScene::BonedRide& b) { return b.object == i; });
        }
        for (const std::uint64_t id : parts)
            scene.attractions.erase(id);
        for (const auto [e, a] : scene.sim.registry.view<const simulation::Attraction>().each())
            if (a.saved == base->id)
            {
                scene.ride_cars.erase(e);
                scene.ride_seats.erase(e);
                scene.ride_seatings.erase(e);
                simulation::pull_down(scene.sim, e);
                break;
            }
        if (ride)
            drop_ride_cars(scene, ref.index);
        if (ride && ref.index < scene.ride_entities.size())
            scene.ride_entities[ref.index] = entt::null;
        base->state = save::model::AttractionState::Closed;
        base->demolished = true;
        simulation::park::book(scene.park, scene.park_model.finance, scene.ledger_month, -back, simulation::park::LedgerLine::RideConstruction, false);
        if (at)
            float_money(scene, *at, back);
        if (scene.shown == ref)
        {
            scene.shown.reset();
            if (auto info = scene.desktop->root().find_as<ui::screens::info::InfoPanel>(kInfoName))
                info->hide();
        }
        logging::logger("park").info("demolished {}, {} back", base->name, back);
        scene.led_signs.stale = true;
        scene.levels = { };
        scene.since_rebuild = kObjectRebuildInterval;
        scene.supports.stale = true;
        refresh_pavings(scene);
        objects_changed(scene);
    }

    void delete_area(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, save::model::Tile a, save::model::Tile b)
    {
        const auto inside = [&](std::int32_t col, std::int32_t row) { return col >= std::min(a.col, b.col) && col <= std::max(a.col, b.col) && row >= std::min(a.row, b.row) && row <= std::max(a.row, b.row); };
        std::vector<std::size_t> props;
        std::vector<rct3::park::AttractionRef> attractions;
        for (std::size_t i = 0; i < scene.objects.size(); ++i)
        {
            const save::model::PlacedObject& o = scene.objects[i];
            if (o.hidden || !inside(o.col, o.row))
                continue;
            if (const auto owner = scene.attractions.find(o.id); owner != scene.attractions.end())
            {
                if (!std::ranges::contains(attractions, owner->second))
                    attractions.push_back(owner->second);
            }
            else if (clickable_prop(scene, o))
                props.push_back(i);
        }
        std::vector<std::uint64_t> paths;
        for (const save::model::PathTile& p : scene.footpaths.paths)
            if (inside(p.col, p.row))
                paths.push_back(p.id);
        delete_props(device, scene, assets, props);
        delete_paths(device, scene, assets, paths);
        ask_demolish(scene, *assets.strings, assets.palette, std::move(attractions));
    }

} // namespace rct3::scene::park
