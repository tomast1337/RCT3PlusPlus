// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park/map_view.hpp"

#include "logging/log.hpp"
#include "ovl/overlay.hpp"
#include "ovl/special_attraction.hpp"
#include "ovl/stall.hpp"
#include "park/map_shapes.hpp"
#include "park/support/sets.hpp"
#include "profiling/profiler.hpp"
#include "strings/ascii.hpp"
#include "strings/unicode.hpp"
#include "ui/game_style.hpp"
#include "ui/screens/map_panel.hpp"
#include "ui/ui_renderer.hpp"

#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace rct3::scene::park
{

    namespace
    {

        constexpr std::string_view kMapName = "park_map";
        constexpr std::string_view kTitleKey = "MapDialog_Title";
        constexpr std::string_view kMapIcon = "Map"; ///< The tool row's Map button shows the same

        /// People move, so their picture is drawn again this often, in seconds.
        constexpr float kPeopleInterval = 0.5f;
        /// The ground the camera sees is cut off this many times its distance from the eye: far enough to
        /// show where it looks, near enough that the horizon does not cover the map.
        constexpr float kFarthestSeen = 3.0f;

        /// A legend row: the layer, the game's text for it where the game has one, else our own words.
        struct LayerText
        {
            rct3::park::MapLayer layer;
            std::string_view key;
            std::u16string_view own;
            bool starts_group = false;
        };
        constexpr std::array kLayerTexts {
            LayerText { rct3::park::MapLayer::Relief, "LoadingScreen_Terrain", u"Terrain" },
            LayerText { rct3::park::MapLayer::Contours, { }, u"Contours" },
            LayerText { rct3::park::MapLayer::Water, { }, u"Water" },
            LayerText { rct3::park::MapLayer::ParkLand, { }, u"Park land" },
            LayerText { rct3::park::MapLayer::Paths, "Obj_PathsHelp", u"Paths", true },
            LayerText { rct3::park::MapLayer::Scenery, "Obj_SceneryHelp", u"Scenery" },
            LayerText { rct3::park::MapLayer::Rides, "MapText_ride", u"Rides", true },
            LayerText { rct3::park::MapLayer::Food, "MapText_foodStall", u"Food" },
            LayerText { rct3::park::MapLayer::Drinks, "MapText_drinkStall", u"Drinks" },
            LayerText { rct3::park::MapLayer::Souvenirs, "MapText_souvenirStall", u"Souvenirs" },
            LayerText { rct3::park::MapLayer::Information, "MapText_infoKiosk", u"Information kiosks" },
            LayerText { rct3::park::MapLayer::FirstAid, "MapText_firstAid", u"First aid" },
            LayerText { rct3::park::MapLayer::CashMachines, "MapText_ATM", u"Cash machines" },
            LayerText { rct3::park::MapLayer::Toilets, "MapText_toilet", u"Toilets" },
            LayerText { rct3::park::MapLayer::Guests, "Graph_Guests", u"Guests", true },
            LayerText { rct3::park::MapLayer::Staff, "Staff_Title", u"Staff" },
            LayerText { rct3::park::MapLayer::Patrols, { }, u"Patrol areas" },
        };
        static_assert(kLayerTexts.size() == rct3::park::kMapLayerCount);

        std::u16string text(const ui::StringTable& strings, std::string_view key, std::u16string_view own)
        {
            const std::u16string_view found = key.empty() ? std::u16string_view { } : strings.get(key);
            return std::u16string(found.empty() ? own : found);
        }

        ui::Color colour(rct3::park::MapRgba c)
        {
            return { c.r, c.g, c.b, c.a };
        }

        ui::screens::MapPanelStyle panel_style(const ui::Palette& palette)
        {
            ui::screens::MapPanelStyle s;
            s.window = ui::game_skins::window_style(palette);
            return s;
        }

        /// The name of `symbol` without its tag.
        std::string_view bare(std::string_view symbol)
        {
            return symbol.substr(0, symbol.find(':'));
        }

        /// The types of the stalls and facilities of `rides`, by their symbols, read from their overlays.
        class AttractionTypes
        {
        public:
            explicit AttractionTypes(const config::GameInstall& install)
                : install_(install)
            {
            }

            std::optional<std::uint32_t> of(const save::model::AttractionCommon& a, bool stall)
            {
                const std::string key = std::format("{}|{}", a.overlay_path, a.symbol);
                if (const auto known = types_.find(key); known != types_.end())
                    return known->second;
                const std::optional<std::uint32_t> type = read(a, stall);
                types_.emplace(key, type);
                return type;
            }

        private:
            std::optional<std::uint32_t> read(const save::model::AttractionCommon& a, bool stall)
            {
                const ovl::Overlay* overlay = open(a.overlay_path);
                if (overlay == nullptr)
                    return std::nullopt;
                const std::string_view name = bare(a.symbol);
                for (const auto part : { core::maybe(&overlay->common()), overlay->unique() })
                {
                    if (!part)
                        continue;
                    if (stall)
                    {
                        for (const ovl::StallItem& item : ovl::read_stalls(*overlay, *part))
                        {
                            if (item.stall && strings::equals_ignoring_case(item.name, name))
                                return item.stall->info.type_id;
                        }
                    }
                    else
                    {
                        for (const ovl::SpecialAttractionItem& item : ovl::read_special_attractions(*overlay, *part))
                        {
                            if (item.attraction && strings::equals_ignoring_case(item.name, name))
                                return item.attraction->info.type_id;
                        }
                    }
                }
                return std::nullopt;
            }

            const ovl::Overlay* open(const std::string& path)
            {
                if (const auto known = overlays_.find(path); known != overlays_.end())
                    return known->second.get();
                std::unique_ptr<ovl::Overlay> overlay;
                if (const auto file = install_.find(std::format("{}.common.ovl", path)))
                {
                    if (auto loaded = ovl::Overlay::load(*file))
                        overlay = std::make_unique<ovl::Overlay>(std::move(*loaded));
                }
                return overlays_.emplace(path, std::move(overlay)).first->second.get();
            }

            const config::GameInstall& install_;
            std::map<std::string, std::unique_ptr<ovl::Overlay>, std::less<>> overlays_;
            std::map<std::string, std::optional<std::uint32_t>, std::less<>> types_;
        };

        /// The ground the camera sees, as map points round it: where the lines through the view's corners
        /// meet the level of the point it looks at, cut off towards the horizon.
        std::vector<ui::Point> seen_ground(const rct3::park::ParkCamera& camera, float aspect, const rct3::park::MapFrame& frame)
        {
            const glm::vec3 eye = rct3::park::eye(camera);
            const float farthest = kFarthestSeen * camera.distance;
            std::vector<ui::Point> corners;
            for (const glm::vec2 at : { glm::vec2 { -1, -1 }, glm::vec2 { 1, -1 }, glm::vec2 { 1, 1 }, glm::vec2 { -1, 1 } })
            {
                const rct3::park::Ray ray = rct3::park::view_ray(camera, aspect, at);
                glm::vec2 ground { ray.direction.x, ray.direction.z };
                const glm::vec2 from { eye.x, eye.z };
                if (ray.direction.y < -1e-4f)
                {
                    const float t = (camera.target.y - ray.origin.y) / ray.direction.y;
                    ground = glm::vec2 { ray.origin.x + ray.direction.x * t, ray.origin.z + ray.direction.z * t } - from;
                }
                const float length = glm::length(ground);
                if (length < 1e-4f)
                    continue;
                if (length > farthest || ray.direction.y >= -1e-4f)
                    ground *= farthest / length;
                const glm::vec2 m = rct3::park::to_map(frame, from + ground);
                corners.push_back({ m.x, m.y });
            }
            return corners;
        }

        /// The mean colour of each surface's picture, by the surface's index.
        std::vector<std::optional<rct3::park::MapRgba>> surface_colours(const rct3::park::ground::GroundImages& images)
        {
            std::vector<std::optional<rct3::park::MapRgba>> colours;
            for (std::size_t index = 0; index <= std::numeric_limits<std::uint8_t>::max(); ++index)
            {
                const auto image = images.find(rct3::park::ground::ground_key(rct3::park::GroundPaint::Surface, static_cast<std::uint8_t>(index)));
                if (image == images.end() || image->second.pixels.empty())
                {
                    colours.emplace_back();
                    continue;
                }
                std::array<double, 3> sum { };
                double weight = 0;
                const std::vector<std::uint8_t>& px = image->second.pixels;
                for (std::size_t i = 0; i + 3 < px.size(); i += 4)
                {
                    const double a = px[i + 3] / 255.0;
                    for (std::size_t c = 0; c < 3; ++c)
                        sum.at(c) += px[i + c] * a;
                    weight += a;
                }
                if (weight <= 0)
                {
                    colours.emplace_back();
                    continue;
                }
                const auto mean = [&](std::size_t c) { return static_cast<std::uint8_t>(std::lround(sum.at(c) / weight)); };
                colours.push_back(rct3::park::MapRgba { mean(0), mean(1), mean(2), 255 });
            }
            return colours;
        }

        void replace(render::Device& device, render::TextureHandle& texture, const rct3::park::MapPicture& picture, std::string_view label)
        {
            if (texture)
                device.destroy_texture(texture);
            texture = { };
            if (picture.rgba.empty())
                return;
            const auto bytes = std::as_bytes(std::span(picture.rgba));
            auto made = device.create_texture({ static_cast<std::uint32_t>(picture.width), static_cast<std::uint32_t>(picture.height), render::TextureFormat::Rgba8, bytes, label });
            if (made)
                texture = *made;
            else
                logging::logger("map").warning("{}: {}", label, made.error());
        }

    } // namespace

    ParkMap park_map(const save::model::TerrainGrid& grid, std::span<const save::model::PathTile> paths, const save::model::WaterLayer& water, std::span<const save::model::PlacedObject> objects, const rct3::park::object::ObjectModels& models, const rct3::park::object::Fitted& fitted, const rct3::park::AttractionObjects& attractions, const save::model::RideModel& rides, const rct3::park::ground::GroundImages& images, const config::GameInstall& install)
    {
        const profiling::Scope zone("Map");
        ParkMap map;
        map.features = rct3::park::map_features(grid, paths, water);
        AttractionTypes types(install);
        const auto layer_of = [&](const save::model::PlacedObject& o) -> std::optional<rct3::park::MapLayer> {
            const auto owner = attractions.find(o.id);
            if (owner == attractions.end())
            {
                // What the park builds under paths and track is not scenery the player placed.
                if (o.overlay == rct3::park::support::kSupportOverlay)
                    return std::nullopt;
                return rct3::park::MapLayer::Scenery;
            }
            const rct3::park::AttractionRef& ref = owner->second;
            if (ref.kind == rct3::park::AttractionRef::Kind::Stall && ref.index < rides.stalls.size())
                return rct3::park::attraction_layer(types.of(rides.stalls[ref.index].base, true));
            if (ref.kind == rct3::park::AttractionRef::Kind::Ride && ref.index < rides.rides.size() && rides.rides[ref.index].kind == save::model::RideKind::Special)
                return rct3::park::attraction_layer(types.of(rides.rides[ref.index].base, false));
            return rct3::park::MapLayer::Rides;
        };
        map.features.shapes = rct3::park::map_shapes(objects, models, grid, fitted, layer_of);
        map.features.surfaces = surface_colours(images);
        std::array<std::size_t, rct3::park::kMapLayerCount> counts { };
        for (const rct3::park::MapShape& shape : map.features.shapes)
            ++counts.at(static_cast<std::size_t>(shape.layer));
        logging::logger("map").info(
            "{} path tiles; shapes: {} scenery, {} rides, {} food, {} drinks, {} souvenirs, {} information, {} first aid, {} cash, {} toilets", map.features.paths.size(), counts[static_cast<std::size_t>(rct3::park::MapLayer::Scenery)], counts[static_cast<std::size_t>(rct3::park::MapLayer::Rides)], counts[static_cast<std::size_t>(rct3::park::MapLayer::Food)], counts[static_cast<std::size_t>(rct3::park::MapLayer::Drinks)], counts[static_cast<std::size_t>(rct3::park::MapLayer::Souvenirs)],
            counts[static_cast<std::size_t>(rct3::park::MapLayer::Information)], counts[static_cast<std::size_t>(rct3::park::MapLayer::FirstAid)], counts[static_cast<std::size_t>(rct3::park::MapLayer::CashMachines)], counts[static_cast<std::size_t>(rct3::park::MapLayer::Toilets)]);
        return map;
    }

    void toggle_park_map(ParkMap& map, ui::Desktop& desktop, const ui::StringTable& strings, const ui::Palette& palette)
    {
        if (auto open = desktop.root().find(kMapName))
        {
            open->close();
            return;
        }
        std::vector<ui::screens::MapLegendEntry> entries;
        for (const LayerText& t : kLayerTexts)
        {
            const bool dots = t.layer >= rct3::park::MapLayer::Food && t.layer != rct3::park::MapLayer::Patrols;
            entries.push_back({ text(strings, t.key, t.own), colour(rct3::park::map_layer_colour(t.layer)), rct3::park::shows(map.layers, t.layer), t.starts_group, dots, std::string(t.key) });
        }
        auto& panel = desktop.root().add<ui::screens::MapPanel>(std::string(kMapName), panel_style(palette), text(strings, kTitleKey, u"Map"), std::move(entries));
        panel.icon = kMapIcon;
        map.stale = true;
        map.since_people = 1e9f;
    }

    std::optional<glm::vec2> update_park_map(render::Device& device, ParkMap& map, ui::Desktop& desktop, const save::model::TerrainGrid& grid, const rct3::park::ParkCamera& camera, float aspect, const std::function<rct3::park::MapPeople()>& people, float elapsed)
    {
        auto panel = desktop.root().find_as<ui::screens::MapPanel>(kMapName);
        if (!panel || panel->closing() || grid.empty())
            return std::nullopt;

        if (panel->take_change())
        {
            for (std::size_t i = 0; i < kLayerTexts.size(); ++i)
                map.layers.set(static_cast<std::size_t>(kLayerTexts[i].layer), panel->shown(i));
            map.stale = true;
            map.since_people = 1e9f;
        }
        if (!map.ground)
            map.ground = profiling::section("MapGround", [&] { return rct3::park::map_ground(grid, map.features, rct3::park::map_scale(grid)); });
        if (map.stale)
        {
            replace(device, map.picture, profiling::section("MapCompose", [&] { return rct3::park::compose_map(*map.ground, grid, map.features, map.layers); }), "park map");
            map.stale = false;
        }
        map.since_people += elapsed;
        if (map.since_people >= kPeopleInterval)
        {
            const rct3::park::MapPeople shown = people();
            replace(device, map.people, rct3::park::people_picture(grid, map.ground->scale, shown.people, map.layers, shown.patrol_tiles), "park map people");
            map.since_people = 0;
        }

        const rct3::park::MapFrame frame = rct3::park::map_frame(grid);
        ui::screens::MapCanvas& canvas = panel->canvas();
        canvas.picture = ui::texture_id(map.picture);
        canvas.people = ui::texture_id(map.people);
        canvas.aspect = frame.width / frame.depth;
        const glm::vec2 target = rct3::park::to_map(frame, { camera.target.x, camera.target.z });
        canvas.camera.target = { target.x, target.y };
        // The way the camera looks, on the map: east is right, north is up.
        canvas.camera.facing = { std::sin(camera.yaw) / frame.width, -std::cos(camera.yaw) / frame.depth };
        canvas.camera.sees = seen_ground(camera, aspect, frame);

        if (const auto pick = canvas.take_pick())
            return rct3::park::to_park(frame, { pick->x, pick->y });
        return std::nullopt;
    }

    void destroy_park_map(render::Device& device, ParkMap& map)
    {
        if (map.picture)
            device.destroy_texture(map.picture);
        if (map.people)
            device.destroy_texture(map.people);
        map.picture = { };
        map.people = { };
    }

} // namespace rct3::scene::park
