// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park_map_view.hpp"

#include "logging/log.hpp"
#include "ovl/overlay.hpp"
#include "ovl/special_attraction.hpp"
#include "ovl/stall.hpp"
#include "park/map_shapes.hpp"
#include "park/support_sets.hpp"
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

namespace rct3::scene
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
            park::MapLayer layer;
            std::string_view key;
            std::u16string_view own;
            bool starts_group = false;
        };
        constexpr std::array kLayerTexts {
            LayerText { park::MapLayer::Relief, "LoadingScreen_Terrain", u"Terrain" },
            LayerText { park::MapLayer::Contours, { }, u"Contours" },
            LayerText { park::MapLayer::Water, { }, u"Water" },
            LayerText { park::MapLayer::ParkLand, { }, u"Park land" },
            LayerText { park::MapLayer::Paths, "Obj_PathsHelp", u"Paths", true },
            LayerText { park::MapLayer::Scenery, "Obj_SceneryHelp", u"Scenery" },
            LayerText { park::MapLayer::Rides, "MapText_ride", u"Rides", true },
            LayerText { park::MapLayer::Food, "MapText_foodStall", u"Food" },
            LayerText { park::MapLayer::Drinks, "MapText_drinkStall", u"Drinks" },
            LayerText { park::MapLayer::Souvenirs, "MapText_souvenirStall", u"Souvenirs" },
            LayerText { park::MapLayer::Information, "MapText_infoKiosk", u"Information kiosks" },
            LayerText { park::MapLayer::FirstAid, "MapText_firstAid", u"First aid" },
            LayerText { park::MapLayer::CashMachines, "MapText_ATM", u"Cash machines" },
            LayerText { park::MapLayer::Toilets, "MapText_toilet", u"Toilets" },
            LayerText { park::MapLayer::Guests, "Graph_Guests", u"Guests", true },
            LayerText { park::MapLayer::Staff, "Staff_Title", u"Staff" },
            LayerText { park::MapLayer::Patrols, { }, u"Patrol areas" },
        };
        static_assert(kLayerTexts.size() == park::kMapLayerCount);

        std::u16string text(const ui::StringTable& strings, std::string_view key, std::u16string_view own)
        {
            const std::u16string_view found = key.empty() ? std::u16string_view { } : strings.get(key);
            return std::u16string(found.empty() ? own : found);
        }

        ui::Color colour(park::MapRgba c)
        {
            return { c.r, c.g, c.b, c.a };
        }

        ui::MapPanelStyle panel_style(const ui::Palette& palette)
        {
            ui::MapPanelStyle s;
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

            std::optional<std::uint32_t> of(const save::AttractionCommon& a, bool stall)
            {
                const std::string key = std::format("{}|{}", a.overlay_path, a.symbol);
                if (const auto known = types_.find(key); known != types_.end())
                    return known->second;
                const std::optional<std::uint32_t> type = read(a, stall);
                types_.emplace(key, type);
                return type;
            }

        private:
            std::optional<std::uint32_t> read(const save::AttractionCommon& a, bool stall)
            {
                const ovl::Overlay* overlay = open(a.overlay_path);
                if (overlay == nullptr)
                    return std::nullopt;
                const std::string_view name = bare(a.symbol);
                for (const auto part : { maybe(&overlay->common()), overlay->unique() })
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
        std::vector<ui::Point> seen_ground(const park::ParkCamera& camera, float aspect, const park::MapFrame& frame)
        {
            const glm::vec3 eye = park::eye(camera);
            const float farthest = kFarthestSeen * camera.distance;
            std::vector<ui::Point> corners;
            for (const glm::vec2 at : { glm::vec2 { -1, -1 }, glm::vec2 { 1, -1 }, glm::vec2 { 1, 1 }, glm::vec2 { -1, 1 } })
            {
                const park::Ray ray = park::view_ray(camera, aspect, at);
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
                const glm::vec2 m = park::to_map(frame, from + ground);
                corners.push_back({ m.x, m.y });
            }
            return corners;
        }

        /// The mean colour of each surface's picture, by the surface's index.
        std::vector<std::optional<park::MapRgba>> surface_colours(const park::GroundImages& images)
        {
            std::vector<std::optional<park::MapRgba>> colours;
            for (std::size_t index = 0; index <= std::numeric_limits<std::uint8_t>::max(); ++index)
            {
                const auto image = images.find(park::ground_key(park::GroundPaint::Surface, static_cast<std::uint8_t>(index)));
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
                colours.push_back(park::MapRgba { mean(0), mean(1), mean(2), 255 });
            }
            return colours;
        }

        void replace(render::Device& device, render::TextureHandle& texture, const park::MapPicture& picture, std::string_view label)
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

    ParkMap park_map(const save::TerrainGrid& grid, std::span<const save::PathTile> paths, const save::WaterLayer& water, std::span<const save::PlacedObject> objects, const park::ObjectModels& models, const park::Fitted& fitted, const park::AttractionObjects& attractions, const save::RideModel& rides, const park::GroundImages& images, const config::GameInstall& install)
    {
        const profiling::Scope zone("Map");
        ParkMap map;
        map.features = park::map_features(grid, paths, water);
        AttractionTypes types(install);
        const auto layer_of = [&](const save::PlacedObject& o) -> std::optional<park::MapLayer> {
            const auto owner = attractions.find(o.id);
            if (owner == attractions.end())
            {
                // What the park builds under paths and track is not scenery the player placed.
                if (o.overlay == park::kSupportOverlay)
                    return std::nullopt;
                return park::MapLayer::Scenery;
            }
            const park::AttractionRef& ref = owner->second;
            if (ref.kind == park::AttractionRef::Kind::Stall && ref.index < rides.stalls.size())
                return park::attraction_layer(types.of(rides.stalls[ref.index].base, true));
            if (ref.kind == park::AttractionRef::Kind::Ride && ref.index < rides.rides.size() && rides.rides[ref.index].kind == save::RideKind::Special)
                return park::attraction_layer(types.of(rides.rides[ref.index].base, false));
            return park::MapLayer::Rides;
        };
        map.features.shapes = park::map_shapes(objects, models, grid, fitted, layer_of);
        map.features.surfaces = surface_colours(images);
        std::array<std::size_t, park::kMapLayerCount> counts { };
        for (const park::MapShape& shape : map.features.shapes)
            ++counts.at(static_cast<std::size_t>(shape.layer));
        logging::logger("map").info(
            "{} path tiles; shapes: {} scenery, {} rides, {} food, {} drinks, {} souvenirs, {} information, {} first aid, {} cash, {} toilets", map.features.paths.size(), counts[static_cast<std::size_t>(park::MapLayer::Scenery)], counts[static_cast<std::size_t>(park::MapLayer::Rides)], counts[static_cast<std::size_t>(park::MapLayer::Food)], counts[static_cast<std::size_t>(park::MapLayer::Drinks)], counts[static_cast<std::size_t>(park::MapLayer::Souvenirs)],
            counts[static_cast<std::size_t>(park::MapLayer::Information)], counts[static_cast<std::size_t>(park::MapLayer::FirstAid)], counts[static_cast<std::size_t>(park::MapLayer::CashMachines)], counts[static_cast<std::size_t>(park::MapLayer::Toilets)]);
        return map;
    }

    void toggle_park_map(ParkMap& map, ui::Desktop& desktop, const ui::StringTable& strings, const ui::Palette& palette)
    {
        if (auto open = desktop.root().find(kMapName))
        {
            open->close();
            return;
        }
        std::vector<ui::MapLegendEntry> entries;
        for (const LayerText& t : kLayerTexts)
        {
            const bool dots = t.layer >= park::MapLayer::Food && t.layer != park::MapLayer::Patrols;
            entries.push_back({ text(strings, t.key, t.own), colour(park::map_layer_colour(t.layer)), park::shows(map.layers, t.layer), t.starts_group, dots, std::string(t.key) });
        }
        auto& panel = desktop.root().add<ui::MapPanel>(std::string(kMapName), panel_style(palette), text(strings, kTitleKey, u"Map"), std::move(entries));
        panel.icon = kMapIcon;
        map.stale = true;
        map.since_people = 1e9f;
    }

    std::optional<glm::vec2> update_park_map(render::Device& device, ParkMap& map, ui::Desktop& desktop, const save::TerrainGrid& grid, const park::ParkCamera& camera, float aspect, const std::function<park::MapPeople()>& people, float elapsed)
    {
        auto panel = desktop.root().find_as<ui::MapPanel>(kMapName);
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
            map.ground = profiling::section("MapGround", [&] { return park::map_ground(grid, map.features, park::map_scale(grid)); });
        if (map.stale)
        {
            replace(device, map.picture, profiling::section("MapCompose", [&] { return park::compose_map(*map.ground, grid, map.features, map.layers); }), "park map");
            map.stale = false;
        }
        map.since_people += elapsed;
        if (map.since_people >= kPeopleInterval)
        {
            const park::MapPeople shown = people();
            replace(device, map.people, park::people_picture(grid, map.ground->scale, shown.people, map.layers, shown.patrol_tiles), "park map people");
            map.since_people = 0;
        }

        const park::MapFrame frame = park::map_frame(grid);
        ui::MapCanvas& canvas = panel->canvas();
        canvas.picture = ui::texture_id(map.picture);
        canvas.people = ui::texture_id(map.people);
        canvas.aspect = frame.width / frame.depth;
        const glm::vec2 target = park::to_map(frame, { camera.target.x, camera.target.z });
        canvas.camera.target = { target.x, target.y };
        // The way the camera looks, on the map: east is right, north is up.
        canvas.camera.facing = { std::sin(camera.yaw) / frame.width, -std::cos(camera.yaw) / frame.depth };
        canvas.camera.sees = seen_ground(camera, aspect, frame);

        if (const auto pick = canvas.take_pick())
            return park::to_park(frame, { pick->x, pick->y });
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

} // namespace rct3::scene
