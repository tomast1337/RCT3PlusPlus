// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park/scene.hpp"

#include "career/career_file.hpp"
#include "config/game/saves.hpp"
#include "core/maybe.hpp"
#include "logging/log.hpp"
#include "ovl/overlay.hpp"
#include "park/chain_lifts.hpp"
#include "park/daylight.hpp"
#include "park/edge_walls.hpp"
#include "park/enclosures.hpp"
#include "park/item_category.hpp"
#include "park/object/models.hpp"
#include "park/object/pick.hpp"
#include "park/object/placement.hpp"
#include "park/person/motions.hpp"
#include "park/ride/placement.hpp"
#include "park/sky/look.hpp"
#include "park/tile_shaping.hpp"
#include "parkfile/saved_park.hpp"
#include "profiling/profiler.hpp"
#include "save/model/park.hpp"
#include "save/model/people.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"
#include "scene/enclosure_tools.hpp"
#include "scene/park/attraction_state.hpp"
#include "scene/park/camera_controls.hpp"
#include "scene/park/deleting.hpp"
#include "scene/park/footpaths.hpp"
#include "scene/park/ground.hpp"
#include "scene/park/holding.hpp"
#include "scene/park/info_panel.hpp"
#include "scene/park/management.hpp"
#include "scene/park/map_view.hpp"
#include "scene/park/patrol.hpp"
#include "scene/park/people.hpp"
#include "scene/park/questions.hpp"
#include "scene/park/refresh.hpp"
#include "scene/park/ride_camera.hpp"
#include "scene/park/setup.hpp"
#include "scene/park/view.hpp"
#include "scene/park/water.hpp"
#include "scene/pool_tools.hpp"
#include "scene/track/building.hpp"
#include "scene/track/trains.hpp"
#include "simulation/animals.hpp"
#include "simulation/awards.hpp"
#include "simulation/components.hpp"
#include "simulation/guests.hpp"
#include "simulation/holding.hpp"
#include "simulation/import_park.hpp"
#include "simulation/parties.hpp"
#include "simulation/rides.hpp"
#include "simulation/staff/care.hpp"
#include "simulation/staff/patrol.hpp"
#include "simulation/stalls.hpp"
#include "simulation/swimming.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"
#include "simulation/vip.hpp"
#include "simulation/weather_cycle.hpp"
#include "strings/ascii.hpp"
#include "strings/unicode.hpp"
#include "ui/extra_skins.hpp"
#include "ui/game_style.hpp"
#include "ui/language.hpp"
#include "ui/screens/build/delete_area.hpp"
#include "ui/screens/build/pools.hpp"
#include "ui/screens/info/attraction.hpp"
#include "ui/screens/park_view/messages.hpp"
#include "ui/screens/park_view/ride_camera.hpp"
#include "ui/screens/park_view/tool_menus.hpp"
#include "ui/screens/park_view/tool_row.hpp"
#include "ui/screens/tutorial/guide.hpp"
#include "ui/thought_text.hpp"
#include "ui/widgets/edit_box.hpp"
#include "ui/widgets/label.hpp"
#include "ui/widgets/question_box.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace rct3::scene::park
{

    namespace
    {

        namespace skins = ui::game_skins;

        constexpr units::TurnRate kViewTurn { 0.15f }; ///< The round view turns round its attraction at
        constexpr float kFocusZoomOut = 2;             ///< A message's camera looks from this many times the round view's distance (ours)
        constexpr float kFocusClosest = 20;            ///< But no closer, so a small stall shows with what is round it (ours)
        // Values for photos at photo spots.
        constexpr units::Radians kPhotoPitch { 0.15f }; ///< Down: near eye level
        constexpr float kPhotoClosest = 3;              ///< Of the eye from the party
        constexpr float kPhotoPersonTall = 1.8f;        ///< Framed from the feet up to this high
        /// Of the pointer between pressing and letting go, in interface units, beyond which it is no click.
        constexpr float kClickSlack = 4;
        constexpr int kFahrenheit = 1;                            ///< The temperature units option's choice for Fahrenheit
        constexpr std::string_view kTapeFence = "KeepClearFence"; ///< Closes a broken ride off until it is fixed
        constexpr std::string_view kTapeFenceOverlay = "Style\\Vanilla\\style";
        constexpr std::string_view kOk = "SMB_Label_OK";
        constexpr std::string_view kPrizeName = "prize_won"; ///< The box telling a prize won, one at a time
        constexpr glm::vec4 kDeleteMarkColour { 1.0f, 0.35f, 0.3f, 0.9f };

        constexpr units::Seconds kHoverPickInterval { 0.1f }; ///< Between looks at what is under the pointer, for its look

        /// The sky and the light as the day clock shows them, with a look that is fading out over them.
        void show_time_of_day(ParkScene& scene, glm::vec3 viewer)
        {
            const simulation::DayClock& day = scene.day;
            const rct3::park::Daylight light = rct3::park::mixed(rct3::park::daylight(day.shown), rct3::park::daylight(day.faded), day.fade);
            scene.light = { .sun_direction = light.sun_direction, .sun = light.sun, .sky = light.sky, .ground = light.ground };
            const float column = rct3::park::sky::sky_column(day.shown);
            const float faded = rct3::park::sky::sky_column(day.faded);
            const float night = glm::mix(rct3::park::sky::sky_darkness(day.shown), rct3::park::sky::sky_darkness(day.faded), day.fade);
            scene.darkness = night;
            scene.sky_mesh = rct3::park::sky::sky_mesh(scene.sky, { .eye = viewer, .radius = kSkyRadius, .day = column, .faded_day = faded, .fade = day.fade, .night = night, .time = scene.park_clock, .tint = rct3::park::weathered(glm::vec3(1.0f), scene.weather_look, 0.6f), .overcast = scene.weather_look.gloom });
            if (!scene.horizon_band.empty())
            {
                const glm::vec4 c = glm::mix(rct3::park::sky::band_colour(scene.horizon_band, column), rct3::park::sky::band_colour(scene.horizon_band, faded), day.fade);
                scene.horizon = { c.r, c.g, c.b, c.a };
            }
            // Clouds hide the sun most and grey the sky; a stroke lights everything up.
            const rct3::park::WeatherLook& weather = scene.weather_look;
            scene.light.sun = rct3::park::weathered(scene.light.sun, weather, 1);
            scene.light.sky = rct3::park::weathered(scene.light.sky, weather, 0.5f);
            scene.light.ground = rct3::park::weathered(scene.light.ground, weather, 0.5f);
            const glm::vec3 horizon = rct3::park::weathered({ scene.horizon.r, scene.horizon.g, scene.horizon.b }, weather, 0.5f);
            scene.horizon = { horizon.r, horizon.g, horizon.b, scene.horizon.a };
            // Far land fades into the horizon's colour, sooner in rain. Chosen by eye.
            constexpr float kRainHaze = 2.5f;
            scene.light.haze = horizon;
            scene.light.haze_rate = scene.haze_rate * (1 + kRainHaze * weather.rain);
            scene.light.surface_detail = scene.surface_detail;
        }

    } // namespace

    namespace
    {

        constexpr float kRidePullPerNotch = 0.5f; ///< Of the ride camera's pull back, per wheel notch
        constexpr float kRidePullMost = 3;        ///< The most it pulls back, which is squared into metres

        /// Whether the object is one of the park's entrances, which a click opens the park's window on.
        bool park_entrance(const ParkScene& scene, const save::model::PlacedObject& o)
        {
            const auto model = scene.models.find(rct3::park::object::model_ref(o));
            return !o.hidden && model != scene.models.end() && model->second.category == std::to_underlying(rct3::park::ItemCategory::ParkEntrance);
        }

        /// What putting a broken path item back new costs: the price of the object it stands as.
        units::Money replace_price(const ParkScene& scene, std::uint64_t object)
        {
            const auto o = std::ranges::find(scene.objects, object, &save::model::PlacedObject::id);
            const auto model = o != scene.objects.end() ? scene.models.find(rct3::park::object::model_ref(*o)) : scene.models.end();
            return { model != scene.models.end() ? model->second.price : 0 };
        }

        /// The build lists' item that the park's content entry `id` names, by its place in them; none before they
        /// are read or when they lack it.
        std::optional<std::size_t> content_item(const ParkScene& scene, std::uint64_t id)
        {
            const auto entry = std::ranges::find(scene.park_model.content.entries, id, &save::model::AssetRequirement::id);
            if (entry == scene.park_model.content.entries.end() || !scene.build.items.valid() || scene.build.items.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return std::nullopt;
            const std::vector<rct3::park::BuildItem>& catalogue = scene.build.items.get();
            const auto item = std::ranges::find_if(catalogue, [&](const rct3::park::BuildItem& b) { return strings::equals_ignoring_case(b.symbol, entry->symbol) && strings::equals_ignoring_case(b.overlay, entry->overlay_stem); });
            return item != catalogue.end() ? std::optional(static_cast<std::size_t>(item - catalogue.begin())) : std::nullopt;
        }

        /// The string key of the name of the park's content entry `id`, as its build list or path style names it;
        /// empty when neither is known.
        std::string_view content_name(const ParkScene& scene, std::uint64_t id)
        {
            if (const auto item = content_item(scene, id))
                return scene.build.items.get()[*item].name_text;
            const auto entry = std::ranges::find(scene.park_model.content.entries, id, &save::model::AssetRequirement::id);
            if (entry == scene.park_model.content.entries.end() || (entry->kind != save::model::AssetKind::PathType && entry->kind != save::model::AssetKind::QueueType))
                return { };
            const auto style = std::ranges::find_if(scene.footpaths.styles, [&](const rct3::park::path::PathStyle& s) { return s.queue == (entry->kind == save::model::AssetKind::QueueType) && strings::equals_ignoring_case(s.name, entry->symbol); });
            return style != scene.footpaths.styles.end() ? std::string_view(style->name_text) : std::string_view { };
        }

        ui::screens::management::ManagementFacts management_facts(const ParkScene& scene)
        {
            const auto broken = simulation::replacement(scene.sim, [&](std::uint64_t object) { return replace_price(scene, object); });
            return {
                .park = &scene.park_model,
                .state = &scene.park,
                .people = &scene.people,
                .rides = &scene.rides,
                .broken_items = broken.items,
                .replace_cost = broken.cost,
                .goals = scene.goal_readings,
                .animal_houses = !scene.sim.registry.view<simulation::AnimalHouse>().empty(),
                .staff_looks = scene.staff_looks,
                .zero_time = scene.park.zero_time,
                .research = &scene.research,
                .research_left = scene.research_items ? std::optional(std::span<const simulation::ResearchItem>(*scene.research_items)) : std::nullopt,
                .research_name = [&scene](std::uint64_t id) { return content_name(scene, id); },
                .takings =
                    [&scene](std::uint64_t id) {
                        ui::screens::management::AttractionTakings t;
                        for (const auto [e, a] : scene.sim.registry.view<simulation::Attraction>().each())
                            if (id != 0 && a.saved == id)
                                t = { .income = t.income + a.income, .customers = t.customers + a.customers };
                        return t;
                    },
            };
        }

        /// Marks the simulated rides and stalls that do not fit the scenery round them, for the park's inspector.
        // shortcut: judged as the park opens and each month, so a ride or stall placed since waits till then; judge
        // on placing too if players notice.
        void judge_themes(ParkScene& scene)
        {
            const std::vector<std::uint64_t> off = simulation::off_theme_attractions(scene.rides, scene.objects, scene.build.items.get());
            for (auto [e, attraction] : scene.sim.registry.view<simulation::Attraction>().each())
                attraction.off_theme = attraction.saved != 0 && std::ranges::contains(off, attraction.saved);
            scene.themes_judged = true;
        }

        /// The attraction whose saved id is `id`, as messages name their subject.
        std::optional<rct3::park::AttractionRef> attraction_with(const ParkScene& scene, std::uint64_t id)
        {
            using Kind = rct3::park::AttractionRef::Kind;
            if (const auto ride = std::ranges::find(scene.rides.rides, id, [](const save::model::Ride& r) { return r.base.id; }); ride != scene.rides.rides.end())
                return rct3::park::AttractionRef { Kind::Ride, static_cast<std::size_t>(ride - scene.rides.rides.begin()) };
            if (const auto stall = std::ranges::find(scene.rides.stalls, id, [](const save::model::Stall& s) { return s.base.id; }); stall != scene.rides.stalls.end())
                return rct3::park::AttractionRef { Kind::Stall, static_cast<std::size_t>(stall - scene.rides.stalls.begin()) };
            return std::nullopt;
        }

        /// What the pointer is on: an attraction, a person of the shown people or a prop of the objects.
        struct Picked
        {
            std::optional<rct3::park::AttractionRef> attraction;
            std::optional<std::size_t> person;
            std::optional<std::size_t> prop;
        };

        /// What is under the pointer at `at`, in interface units on a screen of `size`: the nearest of an
        /// attraction, a prop and a person.
        Picked picked_at(const ParkScene& scene, ui::Point at, ui::Point size)
        {
            if (size.x <= 0 || size.y <= 0)
                return { };
            const glm::vec2 ndc { 2 * at.x / size.x - 1, 1 - 2 * at.y / size.y };
            const rct3::park::Ray ray = rct3::park::view_ray(scene.camera, size.x / size.y, ndc);
            // What is hidden cannot be clicked.
            if (scene.pick_boxes_of != scene.objects.size())
                place_objects(scene);
            const auto object = rct3::park::object::pick_object(ray, scene.pick_boxes, scene.grid, [&](std::size_t i) {
                const save::model::PlacedObject& o = scene.objects[i];
                return scene.show_scenery && !o.hidden && (scene.attractions.contains(o.id) || o.fixture || clickable_prop(scene, o) || park_entrance(scene, o));
            });
            std::vector<glm::mat4> placements;
            if (scene.show_people)
            {
                placements.reserve(scene.shown_people.size());
                for (const ShownPerson& p : scene.shown_people)
                    placements.push_back(scene.simulate && p.guest != entt::null ? glm::translate(glm::mat4(1.0f), standing_at(scene, p)) : p.pose.world);
            }
            const auto person = rct3::park::object::pick_person(ray, placements, scene.grid);
            if (person && (!object || person->distance < object->distance))
                return { .attraction = std::nullopt, .person = person->index, .prop = std::nullopt };
            // A fence the park built takes the click and does nothing with it, as in the game; a changing room
            // opens its panel.
            if (!object || (scene.objects[object->index].fixture && !pool_room(scene, scene.objects[object->index])))
                return { };
            const auto owner = scene.attractions.find(scene.objects[object->index].id);
            if (owner != scene.attractions.end())
                return { .attraction = owner->second, .person = std::nullopt, .prop = std::nullopt };
            return { .attraction = std::nullopt, .person = std::nullopt, .prop = object->index };
        }

        /// The people as the map draws them.
        rct3::park::MapPeople map_people(const ParkScene& scene)
        {
            rct3::park::MapPeople shown;
            shown.people.reserve(scene.shown_people.size());
            for (const ShownPerson& p : scene.shown_people)
                shown.people.push_back({ { p.pose.world[3].x, p.pose.world[3].z }, p.staff });
            for (const auto [_, patrol] : scene.sim.registry.view<const simulation::staff::Patrol>().each())
                shown.patrol_tiles.insert(shown.patrol_tiles.end(), patrol.tiles.begin(), patrol.tiles.end());
            return shown;
        }

        /// The running totals the weekly graphs are counted from: guests come in at the gates and what the
        /// attractions have taken, both since the park was opened, as the simulation counts them. Where money
        /// never runs out the park books nothing, so its takings count nought.
        std::pair<std::int64_t, std::int64_t> week_totals(const ParkScene& scene)
        {
            std::int64_t intake = 0;
            if (const auto* gate = scene.sim.registry.ctx().find<simulation::Turnstile>())
                for (const std::uint32_t n : gate->in)
                    intake += n;
            std::int64_t income = 0;
            if (scene.park.cash)
                for (const auto [e, attraction] : scene.sim.registry.view<const simulation::Attraction>().each())
                    income += attraction.income.value;
            return { intake, income };
        }

        /// Whether a tool's panel is out at the bottom of the screen. A ride's doors are placed with none.
        bool tool_panel_out(const ParkScene& scene)
        {
            const bool placing = scene.placing.item && (scene.placing.step == RidePlacing::Step::Ride || scene.placing.step == RidePlacing::Step::Scenery);
            const bool land = scene.land.tool == LandTools::Tool::Shape || scene.land.tool == LandTools::Tool::Paint || scene.land.tool == LandTools::Tool::Buy;
            return scene.building.ride || placing || land || footpath_panel_open(scene) || pool_panel_open(scene) || scene.deleting.tool == DeleteTools::Tool::Area || scene.structures.tool != StructureTools::Tool::None;
        }

        /// Puts the window of a ride, a stall, a person or a piece of scenery away.
        void hide_info(ParkScene& scene)
        {
            if (auto info = scene.desktop->root().find_as<ui::screens::info::InfoPanel>(kInfoName); info && info->visible)
                info->hide();
            scene.shown.reset();
            scene.shown_person.reset();
            scene.shown_prop.reset();
            scene.shown_party.clear();
        }

        /// One panel at a time at the bottom of the screen: puts the tool out away, with its panel. A ride's doors
        /// being placed stay out with `keep_doors`.
        void put_tools_away(ParkScene& scene, bool keep_doors = false)
        {
            track::close_track_building(scene);
            if (!keep_doors || scene.placing.step == RidePlacing::Step::Ride || scene.placing.step == RidePlacing::Step::Scenery)
                stop_ride_placing(scene);
            close_footpath_panel(scene);
            stop_land_tools(scene);
            stop_deleting(scene);
            stop_structure_tools(scene);
            stop_pooling(scene);
            stop_enclosing(scene);
        }
    } // namespace

    save::model::AttractionCommon* attraction_common(ParkScene& scene, rct3::park::AttractionRef ref)
    {
        if (ref.kind == rct3::park::AttractionRef::Kind::Stall)
            return ref.index < scene.rides.stalls.size() ? &scene.rides.stalls[ref.index].base : nullptr;
        return ref.index < scene.rides.rides.size() ? &scene.rides.rides[ref.index].base : nullptr;
    }

    bool clickable_prop(const ParkScene& scene, const save::model::PlacedObject& o)
    {
        if (scene.attractions.contains(o.id) || o.fixture)
            return false;
        const auto model = scene.models.find(rct3::park::object::model_ref(o));
        return model != scene.models.end() && rct3::park::is_prop(model->second.category) && rct3::park::object::on_owned_land(o, model->second.squares, scene.grid);
    }

    bool typing(const ParkScene& scene)
    {
        // A label has the focus only while its text is selected for copying, and Control+C is a game key too.
        const ui::Widget* focus = scene.desktop ? core::as_pointer(scene.desktop->keyboard_focus()) : nullptr;
        return dynamic_cast<const ui::widgets::EditBox*>(focus) != nullptr || dynamic_cast<const ui::widgets::Label*>(focus) != nullptr;
    }

    bool ripple_at(ParkScene& scene, ui::Point at, ui::Point size)
    {
        if (size.x <= 0 || size.y <= 0)
            return false;
        const glm::vec2 ndc { 2 * at.x / size.x - 1, 1 - 2 * at.y / size.y };
        return ripple_park_water(scene.water, rct3::park::view_ray(scene.camera, size.x / size.y, ndc), scene.grid);
    }

    void update_park_scene(render::Device& device, ParkScene& scene, ui::InterfaceAssets& assets, const config::game::GameOptions& options, const ParkInput& input, const ui::KeyboardState& keyboard, units::Seconds elapsed)
    {
        // The ground is built before the models that give the rides' and stalls' paving its size are read.
        if (scene.footpaths.pavings_stale)
            profiling::section("Ground", [&] { rebuild_ground(device, scene, assets); });
        profiling::section("LedSigns", [&] { update_led_signs(device, scene, assets); });
        const float scale = scale_for(device, scene);
        const render::Extent extent = device.extent();
        ui::Desktop& desktop = *scene.desktop;
        desktop.layout({ 0, 0, static_cast<float>(extent.width) / scale, static_cast<float>(extent.height) / scale });

        // Escape closes what is on top, one thing a press: a person held is put down and a ride left; then the window
        // on top (an item list empties its search first), the open menu, a path trail going on, the tool out with
        // its panel, and the window of a ride or a person. With nothing left it asks to leave, as the guide's quit
        // does, and with that question open it answers it. A window being typed in takes the key itself.
        const bool asking = desktop.root().find(kQuestionName).has_value();
        ui::KeyboardState keys = keyboard;
        const auto is_escape = [](const ui::KeyPress& k) { return k.key == ui::Key::Escape; };
        if (!asking && std::ranges::any_of(keys.presses, is_escape))
        {
            const auto window_of = [&](ui::Widget* w) {
                while (w != nullptr && w->parent() != nullptr && w->parent() != &desktop.root())
                    w = w->parent();
                return w != nullptr && w->name() != kGuideName ? dynamic_cast<ui::widgets::Window*>(w) : nullptr;
            };
            ui::widgets::Window* top = nullptr;
            const auto children = desktop.root().children();
            for (auto it = children.rbegin(); it != children.rend() && top == nullptr; ++it)
                if (auto* window = window_of(it->get()); window != nullptr && window->visible && !window->closing())
                    top = window;
            const auto focus = desktop.keyboard_focus();
            auto tools = desktop.root().find_as<ui::screens::park_view::ToolRow>(kToolsName);
            const bool tool_out = ride_placing(scene) || scene.building.ride || scene.structures.tool != StructureTools::Tool::None || scene.deleting.tool != DeleteTools::Tool::None || scene.pooling.item || scene.enclosing.item || land_tool(scene) || tool_panel_out(scene);
            bool taken = true;
            if (scene.holding)
                hold_person(scene, { }, { }, false, true);
            else if (scene.riding_along())
                stop_riding(scene);
            else if (focus && window_of(&*focus) != nullptr)
                taken = false; // Left in for the window typed in
            else if (top != nullptr)
                top->on_key({ .key = ui::Key::Escape });
            else if (tools && tools->open_menu())
                tools->close_menu();
            else if (drop_footpath_trail(scene))
                ;
            else if (tool_out)
                put_tools_away(scene);
            else if (auto info = desktop.root().find_as<ui::screens::info::InfoPanel>(kInfoName); info && info->visible)
                hide_info(scene);
            else
                ask_leave(scene, *assets.strings, assets.palette);
            if (taken)
                std::erase_if(keys.presses, is_escape);
        }

        // A stall's window shows what it sells by name and picture once those are read.
        if (!scene.carried_read && scene.shown && scene.shown->kind == rct3::park::AttractionRef::Kind::Stall)
        {
            load_carried(device, scene, assets);
            if (auto info = desktop.root().find_as<ui::screens::info::InfoPanel>(kInfoName))
                show_info_page(scene, *info, assets, options);
        }

        // Tracked rides put to test or opened: their trains made ready, as the game does, and run round a whole
        // circuit, a shuttle's track, or an unfinished one to its end, where they crash. While the panel builds an
        // unfinished track, a test of it runs a ghost instead (scene/track/building.hpp); closing the panel readies it again.
        for (const std::size_t ride : std::exchange(scene.readying, { }))
        {
            const auto& course = ride < scene.courses.size() ? scene.courses[ride] : std::nullopt;
            const save::model::Ride& r = scene.rides.rides[ride];
            const auto coaster = course ? simulation::coaster_of(*course, r) : std::nullopt;
            // Boats sail off a dock that is only station, which no train could run round.
            const bool boats = (r.mode == save::model::OperatingMode::Boat || r.mode == save::model::OperatingMode::WaterTransport) && course && std::ranges::contains(course->kinds, rct3::park::track::CourseKind::Station);
            if (!assets.install || r.base.demolished || r.base.state == save::model::AttractionState::Closed || r.mode == save::model::OperatingMode::MiniGolf || (!coaster && !boats && !(course && !course->climbs.empty())))
                continue;
            const bool ghosted = coaster && !coaster->closed && coaster->shuttle == simulation::Shuttle::None && scene.building.ride == ride;
            // A lazy river has no trains: swimmers float round it; nor has a slide climbed on foot.
            if ((r.mode == save::model::OperatingMode::LazyRiver || (!coaster && !boats) || track::set_trains(device, scene, *assets.install, ride, r.train_count.value_or(0), r.cars_per_train.value_or(0))) && !ghosted && (ride >= scene.ride_entities.size() || scene.ride_entities[ride] == entt::null) && !std::ranges::contains(scene.placing.unjoined, ride))
                scene.placing.unjoined.push_back(ride);
            if (auto info = desktop.root().find_as<ui::screens::info::InfoPanel>(kInfoName); info && scene.shown && scene.shown->kind == rct3::park::AttractionRef::Kind::Ride && scene.shown->index == ride)
                show_info_page(scene, *info, assets, options);
        }

        ui::PointerState pointer = input.pointer;
        pointer.position = { pointer.position.x / scale, pointer.position.y / scale };
        scene.current_fonts = &ui::fonts_for(device, assets, desktop.root().rect().width());
        const bool over_interface = profiling::section("Interface", [&] { return desktop.update(pointer, keys, { elapsed, scene.current_fonts }); });
        // Riding along, the park takes no clicks: the whole screen counts as interface.
        const bool over = over_interface || scene.riding_along();
        if (auto panel = scene.riding_along() ? desktop.root().find_as<ui::screens::park_view::RideCameraPanel>(kRideCameraName) : std::nullopt)
        {
            const glm::vec2 moved(input.pointer.position.x - scene.last_pointer.x, input.pointer.position.y - scene.last_pointer.y);
            panel->pointer_moved(glm::length(moved) / scale);
            // Dragging over the view looks round from the seat, the way the pointer goes; a drag begun on the panel
            // does not.
            if (!input.pointer.down)
                scene.ride_dragging = false;
            else if (!over_interface && !scene.ride_dragging)
                scene.ride_dragging = true;
            else if (scene.ride_dragging)
            {
                const glm::vec2 screen(desktop.root().rect().width() * scale, desktop.root().rect().height() * scale);
                const float pitch = options.flag(config::game::GameOption::InvertRideCameraY) ? -1.0f : 1.0f;
                scene.ride_turn = rct3::park::look_round_limited(scene.ride_turn + glm::vec2(-moved.x / screen.x, -pitch * moved.y / screen.y) * rct3::park::kRideLookPerScreen);
            }
            if (!over_interface)
                scene.ride_pull = std::clamp(scene.ride_pull - input.pointer.wheel.y * kRidePullPerNotch, 0.0f, kRidePullMost);
            scene.last_pointer = input.pointer.position;
            if (scene.riding_person && *scene.riding_person >= scene.shown_people.size())
                stop_riding(scene);
            if (const auto action = panel->take_action())
                ride_action(scene, assets, *action);
        }
        if (!scene.riding_along())
            profiling::section("Camera", [&] { move_camera(scene, input, over, options, elapsed); });
        scene.zooming = input.zoom_in_key == input.zoom_out_key ? 0 : (input.zoom_in_key ? 1 : -1);
        auto info = desktop.root().find_as<ui::screens::info::InfoPanel>(kInfoName);
        // What a click would open, for the pointer's look; not looked at every frame, as picking walks the park.
        scene.since_hover_pick += elapsed;
        if (over || !pointer.present || !info || scene.turning || scene.dragging)
            scene.hover_pickable = false;
        else if (scene.since_hover_pick >= kHoverPickInterval)
        {
            const profiling::Scope zone("HoverPick");
            const ui::Rect screen = desktop.root().rect();
            const Picked picked = picked_at(scene, pointer.position, { screen.width(), screen.height() });
            scene.hover_pickable = picked.person || picked.attraction || picked.prop;
            scene.since_hover_pick = { };
        }
        if (info)
        {
            const profiling::Scope zone("InfoPanel");
            // A double click on a ride's round view rides along on it, as the ride camera key does.
            if (info->take_view_double_click() && !scene.riding_along() && scene.shown && scene.shown->kind == rct3::park::AttractionRef::Kind::Ride)
                ride_cam(scene, assets, scene.shown->index);
            // Pressing the park puts open colours away without picking.
            if ((pointer.down || input.right) && !over && info->painting())
                info->close_palette();
            // A stroke paints the patrol area while the left button is held over the park; its page shows the area
            // again when the stroke ends.
            const ui::Rect view = desktop.root().rect();
            if (paint_patrol(scene, pointer.position, { view.width(), view.height() }, pointer.down && !input.right && !input.middle && (scene.patrol_stroke || !over)))
                show_info_page(scene, *info, assets, options);
            // A person picked up hangs over what the pointer is on; a right click puts them back.
            hold_person(scene, pointer.position, { view.width(), view.height() }, false, input.right && !over);
            // A stroke over the ground while the path panel shows its controls lays paths on every tile it spans.
            const bool going = !scene.footpaths.brush.empty() || scene.footpaths.slope_press.has_value();
            footpath_drag(scene, pointer.position, { view.width(), view.height() }, pointer.down && !input.middle && (going || !over), over, input.right, input.shift, input.control);
            update_ride_placing(device, scene, assets, pointer.position, { view.width(), view.height() }, over, input.shift, input.commands);
            update_land_tools(device, scene, assets, pointer.position, { view.width(), view.height() }, pointer.down && !input.right && !input.middle, over, elapsed);
            update_enclosing(device, scene, assets, pointer.position, { view.width(), view.height() }, pointer.down && !input.right && !input.middle, over);
            update_pooling(device, scene, assets, pointer.position, { view.width(), view.height() }, pointer.down && !input.right && !input.middle, over, input.shift, typing(scene) ? std::span<const ui::PlayCommand> { } : std::span(input.commands));
            // The area delete tool marks the tiles a drag spans; its panel clears them or deletes what stands there.
            DeleteTools& deleting = scene.deleting;
            const bool area = deleting.tool == DeleteTools::Tool::Area;
            const auto tile = area && !over ? ground_tile(scene, pointer.position, { view.width(), view.height() }) : std::nullopt;
            if (area && pointer.down && !input.right && !input.middle && (deleting.dragging || tile))
            {
                if (!deleting.dragging)
                    deleting.corner = tile;
                deleting.dragging = true;
                if (tile)
                    deleting.other = tile;
            }
            else
                deleting.dragging = false;
            if (auto panel = desktop.root().find_as<ui::screens::build::DeleteAreaPanel>(kDeleteAreaName); panel && area)
            {
                panel->set_marked(deleting.corner.has_value());
                const auto asked = panel->take_action();
                if (asked == ui::screens::build::DeleteAreaAction::Delete && deleting.corner && deleting.other)
                    delete_area(device, scene, assets, *deleting.corner, *deleting.other);
                if (asked)
                {
                    deleting.corner.reset();
                    deleting.other.reset();
                }
            }
            std::optional<std::array<std::int32_t, 4>> marks;
            if (area && deleting.corner && deleting.other)
                marks = std::array { std::min(deleting.corner->col, deleting.other->col), std::min(deleting.corner->row, deleting.other->row), std::max(deleting.corner->col, deleting.other->col), std::max(deleting.corner->row, deleting.other->row) };
            else if (tile)
                marks = std::array { tile->col, tile->row, tile->col, tile->row };
            if (marks != deleting.drawn)
            {
                rct3::park::TerrainMesh mesh;
                if (marks)
                    rct3::park::tile_marks(mesh, scene.grid, (*marks)[0], (*marks)[1], (*marks)[2], (*marks)[3], rct3::park::TilePart::Whole, kDeleteMarkColour);
                set_view_lines(device, scene.delete_marks, mesh);
                deleting.drawn = marks;
            }
            update_structure_tools(device, scene, assets, pointer.position, { view.width(), view.height() }, pointer.down && !input.right && !input.middle, over, input.shift, input.commands);
            // What the delete menu's tool takes at `at`: an attraction is asked about first; a changing room, a prop
            // or a path goes at once, and the tool stays out.
            const auto delete_under = [&](ui::Point at, ui::Point size) {
                const Picked picked = picked_at(scene, at, size);
                if (picked.attraction)
                    ask_demolish(scene, *assets.strings, assets.palette, { *picked.attraction });
                else if (const auto room = picked.prop ? pool_room(scene, scene.objects[*picked.prop]) : std::nullopt)
                {
                    scene.pools.rooms.erase(scene.pools.rooms.begin() + static_cast<std::ptrdiff_t>(*room));
                    pools_changed(device, scene, assets);
                }
                else if (picked.prop)
                    delete_props(device, scene, assets, std::span(&*picked.prop, 1));
                else if (const auto path = footpath_at(scene, at, size))
                    delete_paths(device, scene, assets, std::span(&*path, 1));
                // What was taken stays among the objects, hidden: the tools out see its room free again.
                scene.placing.solids_of.reset();
                scene.footpaths.solids_of.reset();
            };
            // A click is the left button pressed and let go over the park without the pointer going far.
            if (pointer.down && !scene.press && !over && !input.right && !input.middle)
            {
                scene.press = pointer.position;
                scene.stroke.clear();
            }
            // While pressed, the pointer's way over a pool's water, for the waves it may draw there.
            if (pointer.down && scene.press && view.width() > 0 && view.height() > 0)
            {
                const glm::vec2 ndc { 2 * pointer.position.x / view.width() - 1, 1 - 2 * pointer.position.y / view.height() };
                const rct3::park::Ray ray = rct3::park::view_ray(scene.camera, view.width() / view.height(), ndc);
                if (const auto hit = rct3::park::water::water_hit(ray, scene.water.surface))
                    if (const glm::vec3 at = ray.origin + ray.direction * *hit; rct3::park::pools::on_pool_water(scene.pools, scene.grid, { at.x, at.z }))
                        scene.stroke.push_back({ at.x, at.z, scene.clock });
            }
            if (!pointer.down && scene.press)
            {
                const ui::Point from = *std::exchange(scene.press, std::nullopt);
                const bool still = std::abs(pointer.position.x - from.x) <= kClickSlack && std::abs(pointer.position.y - from.y) <= kClickSlack;
                const ui::Rect screen = desktop.root().rect();
                // While the path panel shows its controls, a click on the ground picks where the path starts.
                // While painting a patrol area, the left button paints and does not pick.
                if (scene.holding)
                    hold_person(scene, pointer.position, { screen.width(), screen.height() }, still && !over, false);
                else if (painting_patrol(scene) || land_tool(scene) || scene.deleting.tool == DeleteTools::Tool::Area || scene.structures.tool == StructureTools::Tool::Grab || scene.enclosing.item || scene.pooling.item)
                    ;
                else if (!still && scene.deleting.tool == DeleteTools::Tool::None && scene.structures.tool == StructureTools::Tool::None && !ride_placing(scene) && !scene.building.ride)
                {
                    // With no tool out, a line or a circle drawn on a pool's water makes waves or a whirlpool there.
                    if (auto wave = rct3::park::pools::drawn_wave(scene.pools, scene.grid, scene.stroke, scene.park_clock))
                    {
                        constexpr std::size_t kMostDrawn = rct3::park::pools::kMaxPoolWaves / 2; ///< Chosen by eye: the rest are the machines'
                        if (scene.drawn_waves.size() >= kMostDrawn)
                            scene.drawn_waves.erase(scene.drawn_waves.begin());
                        scene.drawn_waves.push_back(*wave);
                    }
                }
                else if (still && !over && scene.deleting.tool == DeleteTools::Tool::One)
                    delete_under(pointer.position, { screen.width(), screen.height() });
                else if (still && !over && (structure_tools_click(device, scene, assets) || ride_placing_click(device, scene, assets) || track::track_building_click(scene, assets, pointer.position, { screen.width(), screen.height() })))
                    ;
                else if (still && !over && !footpath_click(scene, pointer.position, { screen.width(), screen.height() }, input.control) && !footpath_panel_open(scene) && !scene.building.ride)
                {
                    // Not with the path panel or track building out: a click they did not take would open a window
                    // in their place and put them away.
                    const Picked picked = picked_at(scene, pointer.position, { screen.width(), screen.height() });
                    if (picked.person)
                        open_person(scene, *info, *picked.person, assets, options);
                    else if (picked.attraction)
                        open_info(scene, *info, *picked.attraction, assets, options);
                    else if (picked.prop)
                    {
                        // An entrance opens the park's window, and its own panel too on the park's land.
                        const save::model::PlacedObject& o = scene.objects[*picked.prop];
                        if (park_entrance(scene, o))
                            open_management_window(desktop, ui::screens::management::ManagementTopic::ParkOperations, 0, management_facts(scene), *assets.strings, assets.palette, info_units(assets, options));
                        // A changing room's window opens on the tab last open in one.
                        if (pool_room(scene, o))
                            open_prop(scene, *info, *picked.prop, assets, options, scene.room_tab.value_or(ui::screens::info::SceneryTab::RoomDetails));
                        else if (clickable_prop(scene, o))
                            open_prop(scene, *info, *picked.prop, assets, options);
                    }
                    else
                        ripple_at(scene, pointer.position, { screen.width(), screen.height() });
                }
            }
            if (input.right || input.middle)
                scene.press.reset();
            // A right click deletes what is under the pointer with the tools that build or place things one by one, as
            // the delete menu's tools do, and with the structure and land tools; the tool stays out. Escape puts
            // any away.
            if (input.right && !scene.right_press && !over)
                scene.right_press = pointer.position;
            else if (!input.right && scene.right_press)
            {
                const ui::Point from = *std::exchange(scene.right_press, std::nullopt);
                const ui::Rect screen = desktop.root().rect();
                // Only a still click: a right drag moves the camera and leaves the tool out, as in the game.
                const bool still = std::abs(pointer.position.x - from.x) <= kClickSlack && std::abs(pointer.position.y - from.y) <= kClickSlack;
                if (!still)
                    ;
                else if (scene.pooling.item)
                {
                    // A pool tool takes up the pool square pointed at first.
                    if (!over && !delete_pool_square(device, scene, assets, pointer.position, { screen.width(), screen.height() }))
                        delete_under(pointer.position, { screen.width(), screen.height() });
                }
                else if (ride_placing(scene) || scene.deleting.tool != DeleteTools::Tool::None || scene.enclosing.item || scene.structures.tool != StructureTools::Tool::None)
                {
                    if (!over)
                        delete_under(pointer.position, { screen.width(), screen.height() });
                }
                else if (land_tool(scene))
                {
                    // A waterfall pointed at goes first.
                    if (!over && !delete_waterfall(device, scene, assets, pointer.position, { screen.width(), screen.height() }))
                        delete_under(pointer.position, { screen.width(), screen.height() });
                }
                else if (!over)
                    (void)footpath_sell(scene, pointer.position, { screen.width(), screen.height() });
            }
            // The customers page follows the running park; it has no controls to lose a half-made change in. Any page
            // shows again once a crash closed its ride.
            scene.since_info_page += elapsed;
            if (scene.shown && (std::exchange(scene.shown_closed, false) || (scene.simulate && scene.info_tab == ui::screens::info::AttractionTab::Customers && scene.since_info_page >= kLivePageInterval)))
            {
                scene.since_info_page = { };
                show_info_page(scene, *info, assets, options);
            }
            const auto action = info->take_action();
            if (action && scene.shown_prop && *action < scene.prop_tabs.size() && scene.prop_tabs[*action] == ui::screens::info::SceneryTab::Destroy)
            {
                // Pulled down at once and paid for, as in the game; the panel goes with it.
                if (delete_props(device, scene, assets, std::span(&*scene.shown_prop, 1)).has_value())
                {
                    scene.shown_prop.reset();
                    info->hide();
                }
            }
            // A changing room's construction tab opens the pool panel, to build on at its complex.
            if (action && scene.shown_prop && *action < scene.prop_tabs.size() && scene.prop_tabs[*action] == ui::screens::info::SceneryTab::RoomConstruction)
                if (const auto pools = scene.desktop->root().find_as<ui::screens::build::PoolPanel>("pool_panel"); !pools || !pools->showing())
                    toggle_pool_panel(device, scene, assets, info_units(assets, options).currency);
            if (action && scene.shown_person && *action < scene.person_tabs.size() && scene.person_tabs[*action] == ui::screens::info::PersonTab::Party)
                open_party(scene, *info, *scene.shown_person, assets, options);
            if (action && scene.shown && *action < scene.shown_tabs.size() && scene.shown_tabs[*action] == ui::screens::info::AttractionTab::Construction)
                scene.construct_requested = *scene.shown;
            const auto door = [](const ui::screens::info::InfoChange& c) { return c.id == ui::screens::info::control_id(ui::screens::info::AttractionControl::Entrance) || c.id == ui::screens::info::control_id(ui::screens::info::AttractionControl::Exit); };
            // A tracked ride's entrance or exit goes beside its station, a flat ride's round its ground, placed as its
            // kind's item has it; a flat ride takes its own item, whose size it stands on.
            const auto start_door = [&](std::size_t ride, bool exit) {
                const auto items = scene.build.items.valid() && scene.build.items.wait_for(std::chrono::seconds(0)) == std::future_status::ready ? std::span(scene.build.items.get()) : std::span<const rct3::park::BuildItem> { };
                std::string overlay = scene.rides.rides.at(ride).base.overlay_path;
                std::ranges::replace(overlay, '\\', '/');
                // A theme's flat rides share its overlay; the symbol, before its type tag, tells them apart.
                const std::string_view symbol = scene.rides.rides[ride].base.symbol;
                const std::string_view name = symbol.substr(0, symbol.find(':'));
                const auto in_overlay = [&](const rct3::park::BuildItem& i) { return !i.entrance.empty() && strings::equals_ignoring_case(i.overlay, overlay); };
                auto kind = std::ranges::find_if(items, [&](const rct3::park::BuildItem& i) { return in_overlay(i) && strings::equals_ignoring_case(i.symbol, name); });
                if (kind == items.end())
                    kind = std::ranges::find_if(items, in_overlay);
                const auto any = std::ranges::find_if(items, [](const rct3::park::BuildItem& i) { return !i.entrance.empty(); });
                const auto item = kind != items.end() || scene.rides.rides[ride].kind == save::model::RideKind::Flat ? kind : any;
                return item != items.end() && start_door_placing(scene, ride, *item, track::station_pieces(scene, ride, assets), exit);
            };
            // A closed circuit goes on to its doors, the entrance first, while its window shows, as in the game.
            if (const auto ride = std::exchange(scene.doors_requested, std::nullopt); ride && *ride < scene.rides.rides.size())
            {
                const auto& stations = scene.rides.rides[*ride].stations;
                const bool entrance = !stations.empty() && stations.front().entrance_spot.has_value();
                if ((!entrance || stations.front().exit_spot == std::nullopt) && start_door(*ride, entrance))
                    stop_land_tools(scene);
            }
            if (const auto change = info->take_change(); change && scene.shown && scene.shown->kind == rct3::park::AttractionRef::Kind::Ride && door(*change))
            {
                if (start_door(scene.shown->index, change->id == ui::screens::info::control_id(ui::screens::info::AttractionControl::Exit)))
                {
                    stop_land_tools(scene);
                    info->hide();
                }
            }
            else if (change && scene.shown && change->id == ui::screens::info::control_id(ui::screens::info::AttractionControl::Demolish))
                ask_demolish(scene, *assets.strings, assets.palette, { *scene.shown });
            // A pool ride's join button opens the pool panel on its paths page, the window staying, as in the game.
            else if (change && scene.shown && change->id == ui::screens::info::control_id(ui::screens::info::AttractionControl::JoinPool))
                toggle_pool_panel(device, scene, assets, info_units(assets, options).currency, ui::screens::build::PoolPage::Paths);
            else if (change && scene.shown)
            {
                change_attraction(device, scene, *info, *scene.shown, *change, assets, options);
                // Longer trains fit fewer times round the track, so the page shows the new limits; a new name and
                // the graph's series show.
                if (change->id == ui::screens::info::control_id(ui::screens::info::AttractionControl::Trains) || change->id == ui::screens::info::control_id(ui::screens::info::AttractionControl::Cars) || change->id == ui::screens::info::control_id(ui::screens::info::AttractionControl::Name) || change->id == ui::screens::info::control_id(ui::screens::info::AttractionControl::CarKind)
                    || (change->id >= ui::screens::info::control_id(ui::screens::info::AttractionControl::SpeedSeries) && change->id <= ui::screens::info::control_id(ui::screens::info::AttractionControl::LateralSeries)))
                    show_info_page(scene, *info, assets, options);
            }
            else if (const auto room = change && scene.shown_prop ? pool_room(scene, scene.objects[*scene.shown_prop]) : std::nullopt)
            {
                const save::model::PlacedObject& o = scene.objects[*scene.shown_prop];
                if (change->id == ui::screens::info::kPoolDeleteRoomControl)
                {
                    scene.pools.rooms.erase(scene.pools.rooms.begin() + static_cast<std::ptrdiff_t>(*room));
                    pools_changed(device, scene, assets);
                    scene.shown_prop.reset();
                    info->hide();
                }
                else if (change->id == ui::screens::info::kPoolDeleteComplexControl)
                    ask_sell_pool_complex(scene, *assets.strings, assets.palette, *room);
                else if (change->id == ui::screens::info::kPoolMechanicControl)
                {
                    if (const entt::entity e = room_entity(scene, *room); e != entt::null)
                        scene.sim.registry.get_or_emplace<simulation::Upkeep>(e).called = true;
                }
                else if (change->id == ui::screens::info::kPoolInspectionControl)
                {
                    // Each room is inspected on its own, as each ride is.
                    if (const entt::entity e = room_entity(scene, *room); e != entt::null && change->value >= 0 && std::cmp_less(change->value, simulation::kInspectionSeconds.size()))
                        scene.sim.registry.get_or_emplace<simulation::Upkeep>(e).inspect_every = { simulation::kInspectionSeconds[static_cast<std::size_t>(change->value)] };
                }
                else if (change->id == ui::screens::info::kPoolLocateControl)
                {
                    const auto model = scene.models.find(rct3::park::object::model_ref(o));
                    scene.camera.target = glm::vec3(rct3::park::object::placed_transform(o, model == scene.models.end() ? nullptr : &model->second, scene.grid, scene.fitted)[3]);
                }
                else
                {
                    // The whole complex is opened or closed, takes the ticket price, the name and the wave
                    // settings, all its rooms alike. A blank name keeps the old one, as an attraction's does.
                    for (const std::size_t r : rct3::park::pools::pool_complex_rooms(scene.pools, *room))
                    {
                        if (change->id == ui::screens::info::kPoolTicketControl)
                            scene.pools.rooms[r].ticket = change->value;
                        else if (change->id == ui::screens::info::kPoolFluidControl)
                            scene.pools.rooms[r].fluid = static_cast<std::uint8_t>(std::clamp(change->value, 0, 2));
                        else if (change->id == ui::screens::info::kPoolWaveHeightControl)
                            scene.pools.rooms[r].waves.height = static_cast<float>(change->value);
                        else if (change->id == ui::screens::info::kPoolWaveLengthControl)
                            scene.pools.rooms[r].waves.frequency = rct3::park::pools::kLeastWaveFrequency + rct3::park::pools::kMostWaveFrequency - static_cast<float>(change->value);
                        else if (change->id == ui::screens::info::kPoolWaveOnControl)
                            scene.pools.rooms[r].waves.on = static_cast<float>(change->value);
                        else if (change->id == ui::screens::info::kPoolWaveEveryControl)
                            scene.pools.rooms[r].waves.every = static_cast<float>(change->value) * 60;
                        else if (change->id == ui::screens::info::kPoolNameControl && change->text.find_first_not_of(u' ') != std::u16string::npos)
                            scene.pools.rooms[r].complex_name = strings::to_utf8(change->text);
                    }
                    open_prop(scene, *info, *scene.shown_prop, assets, options, scene.prop_tab);
                }
            }
            else if (change && !scene.shown_party.empty())
            {
                if (const auto member = static_cast<std::size_t>(change->id); member < scene.shown_party.size())
                    open_person(scene, *info, scene.shown_party[member], assets, options);
            }
            else if (change && scene.shown_person)
            {
                if (change->id == ui::screens::info::person_control(ui::screens::info::PersonControl::PickUp))
                {
                    // Only those walking in the park; riders are left be.
                    if (!pick_up_person(scene, *scene.shown_person) && (scene.shown_people[*scene.shown_person].guest == entt::null || scene.sim.registry.all_of<simulation::Left>(scene.shown_people[*scene.shown_person].guest)))
                        simulation::park::post(scene.park.messages, { .text_key = "Pincer_NotOwned" });
                }
                else if (change->id == ui::screens::info::person_control(ui::screens::info::PersonControl::Sack))
                {
                    fire(scene, *scene.shown_person);
                    scene.shown_person.reset();
                    info->hide();
                }
                else if (change_person(scene, *scene.shown_person, *change))
                {
                    show_info_page(scene, *info, assets, options);
                    if (change->id == ui::screens::info::person_control(ui::screens::info::PersonControl::Name))
                        name_cheats(scene, assets, *scene.shown_person, change->text);
                }
            }
            const auto paint = info->take_paint();
            if (paint && scene.shown_person)
                paint_person(scene, *scene.shown_person, paint->pot, paint->colour);
            if (paint && scene.shown_prop)
            {
                // Pot k paints the k-th slot the prop takes; free, as in the game.
                const std::vector<std::size_t> slots = ui::screens::info::scenery_paint_slots(prop_facts(scene, *scene.shown_prop));
                if (paint->pot < slots.size())
                    paint_prop(device, scene, *scene.shown_prop, slots[paint->pot], static_cast<std::int32_t>(paint->colour));
            }
            if (const auto tab = info->take_picked(); tab && scene.shown_prop)
            {
                if (*tab < scene.prop_tabs.size())
                {
                    scene.prop_tab = scene.prop_tabs[*tab];
                    if (pool_room(scene, scene.objects[*scene.shown_prop]))
                        scene.room_tab = scene.prop_tab;
                    show_info_page(scene, *info, assets, options);
                }
            }
            else if (tab && !scene.shown_party.empty())
            {
                if (*tab < ui::screens::info::kPartyTabs.size())
                {
                    scene.party_tab = ui::screens::info::kPartyTabs[*tab].tab;
                    show_info_page(scene, *info, assets, options);
                }
            }
            else if (tab && scene.shown_person)
            {
                if (*tab < scene.person_tabs.size())
                {
                    scene.person_tab = scene.person_tabs[*tab];
                    show_info_page(scene, *info, assets, options);
                }
            }
            else if (tab && *tab < scene.shown_tabs.size())
            {
                scene.info_tab = scene.shown_tabs[*tab];
                scene.info_section = 0;
                show_info_page(scene, *info, assets, options);
            }
            if (const auto section = info->take_section())
            {
                scene.info_section = *section;
                show_info_page(scene, *info, assets, options);
            }
            const auto choice = info->take_choice();
            if (const auto room = choice && scene.shown_prop ? pool_room(scene, scene.objects[*scene.shown_prop]) : std::nullopt)
            {
                // The whole complex opened or closed, all its rooms alike; then shown again for the flag's new look.
                const std::vector<ui::screens::info::StateChoice> states = ui::screens::info::state_choices(ui::screens::info::AttractionKind::Stall);
                if (choice->second < states.size())
                    for (const std::size_t r : rct3::park::pools::pool_complex_rooms(scene.pools, *room))
                        scene.pools.rooms[r].open = states[choice->second].state == save::model::AttractionState::Open;
                open_prop(scene, *info, *scene.shown_prop, assets, options, scene.prop_tab);
            }
            if (choice && scene.shown)
            {
                // Put in the state picked, then shown again for the state tab's new look.
                const ui::screens::info::AttractionFacts f = facts(scene, *scene.shown);
                const std::vector<ui::screens::info::StateChoice> states = state_choices(scene, *scene.shown);
                if (save::model::AttractionCommon* common = attraction_common(scene, *scene.shown); common != nullptr && choice->second < states.size())
                {
                    const save::model::AttractionState state = states[choice->second].state;
                    scene.refusal.clear();
                    if (f.ride != nullptr)
                        scene.refusal = change_ride_state(scene, scene.shown->index, state).value_or("");
                    else
                        scene.refusal = change_stall_state(scene, scene.shown->index, state).value_or("");
                    open_info(scene, *info, *scene.shown, assets, options);
                }
            }
            if (info->take_closed())
            {
                scene.shown.reset();
                scene.shown_person.reset();
                scene.shown_prop.reset();
                scene.shown_party.clear();
            }
        }
        if (const auto asked = std::exchange(scene.rename_requested, std::nullopt); asked && change_person(scene, asked->first, ui::screens::info::InfoChange { .id = ui::screens::info::person_control(ui::screens::info::PersonControl::Name), .text = asked->second }))
            name_cheats(scene, assets, asked->first, asked->second);
        if (const auto asked = std::exchange(scene.info_requested, std::nullopt); asked && info)
            open_info(scene, *info, *asked, assets, options);
        if (const auto asked = std::exchange(scene.person_requested, std::nullopt); asked && info && *asked < scene.shown_people.size())
            open_person(scene, *info, *asked, assets, options);
        // The Construction tab has no page: it opens the panel that builds the ride's track.
        if (const auto asked = std::exchange(scene.construct_requested, std::nullopt); asked && asked->kind == rct3::park::AttractionRef::Kind::Ride)
        {
            // An open ride's track stays as it is; testing or closed, it may be changed.
            if (asked->index < scene.rides.rides.size() && scene.rides.rides[asked->index].base.state == save::model::AttractionState::Open)
            {
                scene.refusal = "CannotEditTrackedRideWhileOpen";
                if (info && scene.shown)
                    open_info(scene, *info, *scene.shown, assets, options);
            }
            else if (const auto overlay = track::ride_track_overlay(scene, asked->index))
            {
                if (track::open_track_building(scene, asked->index, *overlay, assets))
                {
                    close_footpath_panel(scene); // One tool panel at the bottom left at a time
                    stop_land_tools(scene);
                    stop_deleting(scene);
                }
            }
        }
        profiling::section("TrackBuilding", [&] {
            const ui::Rect screen = desktop.root().rect();
            track::update_track_building(device, scene, assets, options.choice(config::game::GameOption::Currency), info_units(assets, options).distance, pointer.position, { screen.width(), screen.height() }, over, input.shift, input.commands, elapsed);
        });
        // A slide built up to a pool, or taken away, opens or closes its railing there.
        if (!scene.pools.squares.empty() && pool_openings(scene) != scene.pool_openings)
            pools_changed(device, scene, assets);
        scene.view_camera.yaw += kViewTurn.value * elapsed.value;
        if (scene.shown_person && *scene.shown_person < scene.shown_people.size())
            scene.view_camera = person_view(scene, *scene.shown_person, scene.view_camera.yaw);
        const glm::vec3 viewer = rct3::park::eye(scene.camera, scene.seat);
        profiling::section("Objects", [&] { refresh_objects(device, scene, viewer, elapsed); });
        if (scene.billboards_eye != viewer)
        {
            const profiling::Scope zone("Billboards");
            for (ShownObjects* shown : { &scene.steady_objects, &scene.finer_objects, &scene.coarser_objects })
                rct3::park::object::face_billboards(shown->billboards, viewer);
            scene.billboards_eye = viewer;
        }
        scene.clock += elapsed.value;
        const units::Seconds park_time { elapsed.value * simulation::time_factor(scene.park.speed) };
        scene.park_clock += park_time.value;
        scene.chain_shifts.resize(scene.rides.rides.size());
        for (std::size_t i = 0; i < scene.chain_shifts.size(); ++i)
            scene.chain_shifts[i] = rct3::park::chain_shift(scene.chain_shifts[i], scene.rides.rides[i].options.lift_speed, park_time.value);
        advance_park_particles(scene.particles, park_time, viewer, static_cast<std::size_t>(options.get(config::game::GameOption::ParticleBudget)));
        scene.water.look = water_look(options);
        scene.shadows = options.choice(config::game::GameOption::Shadows);
        scene.blend_shadows = options.flag(config::game::GameOption::BlendShadows);
        scene.blend_band = options.get(config::game::GameOption::DetailBlendBand);
        scene.level_scales = level_scales(options);
        scene.bloom = options.flag(config::game::GameOption::Bloom);
        scene.light_shafts = options.flag(config::game::GameOption::LightShafts);
        scene.ambient_occlusion = options.flag(config::game::GameOption::AmbientOcclusion);
        scene.surface_detail = options.choice(config::game::GameOption::SurfaceDetail);
        advance_park_water(scene.water, park_time);
        rct3::park::pools::drop_spent_waves(scene.drawn_waves, scene.park_clock);
        rct3::park::pools::drop_spent_waves(scene.rings, scene.park_clock);
        scene.water.pool_waves = scene.machine_waves;
        scene.water.pool_waves.insert(scene.water.pool_waves.end(), scene.drawn_waves.begin(), scene.drawn_waves.end());
        scene.water.pool_waves.insert(scene.water.pool_waves.end(), scene.rings.begin(), scene.rings.end());
        profiling::section("LensWater", [&] {
            using save::model::WeatherType;
            const WeatherType w = scene.park.weather;
            std::optional<std::pair<std::size_t, std::uint32_t>> splashes;
            if (scene.riding && *scene.riding < scene.car_waters.size())
                splashes = std::pair { *scene.riding, scene.car_waters[*scene.riding].splashes };
            const LensSurroundings around {
                .view_projection = rct3::park::view_projection(scene.camera, aspect(device), scene.seat, scene.seat_across),
                .eye = viewer,
                .aspect = aspect(device),
                .under_water = scene.water.eye_in.has_value(),
                .rain = w == WeatherType::LightRain  ? 1
                    : w == WeatherType::HeavyRain    ? 2
                    : w == WeatherType::Thunderstorm ? 3
                                                     : 0,
                .wind = scene.park.wind,
                .car_splashes = splashes,
            };
            // As the game has it, drops are drawn only where refraction is.
            advance_park_lens(scene.lens, around, options.flag(config::game::GameOption::WaterDroplets) && options.flag(config::game::GameOption::Refraction), elapsed);
        });
        profiling::section("People", [&] { refresh_people(scene, viewer, rct3::park::view_projection(scene.camera, aspect(device), scene.seat, scene.seat_across), elapsed); });
        profiling::section("ZooAnimals", [&] { place_animals(scene); });
        auto bar = desktop.root().find_as<ui::screens::park_view::ParkBar>(kBarName);
        if (bar)
        {
            if (const auto asked = bar->take_speed())
                scene.park.speed = *asked;
            if (const auto mode = bar->take_day_mode())
                simulation::change_mode(scene.day, *mode);
            if (bar->objective_button().take_click())
                open_management_window(desktop, ui::screens::management::ManagementTopic::Objectives, 0, management_facts(scene), *assets.strings, assets.palette, info_units(assets, options));
            if (bar->take_finances())
                open_management_window(desktop, ui::screens::management::ManagementTopic::Finances, 0, management_facts(scene), *assets.strings, assets.palette, info_units(assets, options));
        }
        for (const ui::PlayCommand c : input.commands)
        {
            if (c == ui::PlayCommand::TogglePause)
                scene.park.speed = simulation::pause_toggled(scene.park.speed);
            // The ride camera key leaves, or rides along on the ride whose window is open.
            else if (c == ui::PlayCommand::ToggleRideCamera && scene.riding_along())
                stop_riding(scene);
            else if (c == ui::PlayCommand::ToggleRideCamera && scene.shown && scene.shown->kind == rct3::park::AttractionRef::Kind::Ride)
                ride_cam(scene, assets, scene.shown->index);
            else if (c == ui::PlayCommand::NextRideCamera)
                ride_action(scene, assets, ui::screens::park_view::RideCameraAction::NextCamera);
            else if (c == ui::PlayCommand::PreviousRideCamera)
                ride_action(scene, assets, ui::screens::park_view::RideCameraAction::PreviousCamera);
            else if (c == ui::PlayCommand::NextRide)
                ride_action(scene, assets, ui::screens::park_view::RideCameraAction::NextTrain);
            else if (c == ui::PlayCommand::PreviousRide)
                ride_action(scene, assets, ui::screens::park_view::RideCameraAction::PreviousTrain);
            else if (c == ui::PlayCommand::NextCoasterCamType)
                ride_action(scene, assets, ui::screens::park_view::RideCameraAction::ChangeType);
        }
        profiling::section("Simulation", [&] {
            // The park's day, its calendar and its messages' ages run at the speed picked, and stop while paused.
            const float game_seconds = elapsed.value * simulation::time_factor(scene.park.speed);
            simulation::advance(scene.day, game_seconds);
            const float before = scene.park.time;
            const std::int32_t weeks = simulation::park::pass_time(scene.park, game_seconds);
            simulation::advance_weather(scene.park, scene.park.time - before, scene.gameplay, options.flag(config::game::GameOption::DisableWeather));
            scene.thunder = rct3::park::advance_weather_look(scene.weather_look, scene.park.weather, scene.park.speed.paused ? 0.0f : elapsed.value, scene.weather_dice);
            // Research, once the build lists say what each item is. It goes by the calendar, so that a calendar set
            // on by hand takes research with it.
            if (scene.build.items.valid() && scene.build.items.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                const std::vector<rct3::park::BuildItem>& catalogue = scene.build.items.get();
                if (!scene.research_items)
                {
                    read_path_styles(scene.footpaths, assets);
                    scene.research_items = simulation::research_items(scene.park_model.content, catalogue, scene.footpaths.styles);
                }
                const std::size_t had = scene.research.invented.size();
                simulation::step_research(scene.research, scene.park.time - scene.research_time.value_or(scene.park.time), *scene.research_items, scene.park, scene.park_model.finance, scene.ledger_month, scene.gameplay);
                // Each new invention can be built from now on, and the player is told.
                for (std::size_t i = had; i < scene.research.invented.size(); ++i)
                {
                    const auto entry = std::ranges::find(scene.park_model.content.entries, scene.research.invented[i], &save::model::AssetRequirement::id);
                    if (entry == scene.park_model.content.entries.end())
                        continue;
                    entry->invented = true;
                    using save::model::AssetKind;
                    const auto item = content_item(scene, entry->id);
                    const std::string_view key = entry->kind == AssetKind::Scenery ? simulation::kInventedSceneryKey : entry->kind == AssetKind::PathType ? simulation::kInventedPathKey : entry->kind == AssetKind::QueueType ? simulation::kInventedQueueKey : simulation::kInventedKey;
                    // Named as its build list names it.
                    const std::u16string_view name = assets.strings->get_ignoring_case(content_name(scene, entry->id));
                    simulation::park::post(scene.park.messages, { .text_key = std::string(key), .subject_name = name.empty() ? entry->symbol : strings::to_utf8(name), .build_item = item });
                }
                if (scene.research.invented.size() != had)
                    scene.build.offer = rct3::park::BuildOffer(scene.park_model.content);
                // shortcut: an open list shows the progress it had when opened; refresh it if players watch it grow.
                scene.build.researching = std::nullopt;
                if (const auto progress = simulation::research_progress(scene.research, *scene.research_items))
                {
                    if (const auto item = content_item(scene, progress->id))
                        scene.build.researching = std::pair { *item, progress->done };
                }
            }
            scene.research_time = scene.park.time;
            // Each new week rates the park again; weeks run through in one frame are rated once.
            if (weeks > 0)
            {
                // While the park runs, its own litter counts, and much of it is told to the player.
                const std::optional<std::int32_t> old = scene.simulate ? std::optional(simulation::old_litter(scene.sim)) : std::nullopt;
                profiling::section("Rating", [&] { simulation::park::rate_week(scene.park, scene.people, scene.rides, old); });
                if (old >= kLitterWarning)
                    simulation::park::post(scene.park.messages, { .text_key = "Thought_PI_Park_Litter1" });
                {
                    // The week's figures for the park's graphs.
                    std::array<std::int32_t, 3> ages { };
                    for (const auto [e, visitor] : scene.sim.registry.view<const simulation::Visitor>(entt::exclude<simulation::Left>).each())
                        ++ages[static_cast<std::size_t>(visitor.age)];
                    const auto [intake, income] = week_totals(scene);
                    const std::int64_t cash = scene.park.cash.value_or(units::Money { }).value;
                    auto& began = scene.week_began;
                    simulation::park::close_week(
                        scene.park_model,
                        {
                            .rating = static_cast<std::int32_t>(std::lround(scene.park.rating.value_or(0) * simulation::park::kMaxRating)),
                            .guests = scene.park.guests,
                            .adults = ages[static_cast<std::size_t>(simulation::AgeGroup::Adult)],
                            .teenagers = ages[static_cast<std::size_t>(simulation::AgeGroup::Teen)],
                            .children = ages[static_cast<std::size_t>(simulation::AgeGroup::Child)],
                            .intake = static_cast<std::int32_t>(intake - began.intake),
                            .cash_less_loan = units::Money { cash } - scene.park.loan,
                            .income = units::Money { income - began.income },
                            .profit = units::Money { cash - began.cash },
                            .park_value = simulation::park::park_value(scene.park.guests, scene.rides),
                        });
                    began = { .intake = intake, .income = income, .cash = cash };
                }
                // Awards, as each month begins, from the park as it runs. The month the park opens in is not judged.
                const bool new_month = scene.awards_month && scene.park.date.month != *scene.awards_month;
                scene.awards_month = scene.park.date.month;
                if (new_month && scene.simulate && scene.park_model.has_awards)
                {
                    const entt::registry& r = scene.sim.registry;
                    const save::model::Finance& money = scene.park_model.finance;
                    simulation::AwardFigures f {
                        .guests = scene.park.guests,
                        .rating = static_cast<std::int32_t>(std::lround(scene.park.rating.value_or(0) * simulation::park::kMaxRating)),
                        .money = scene.park.cash.has_value(),
                        .fee_charged = scene.park.cash.has_value() && !money.unlimited() && !money.entrance_fee_fixed(),
                        .entrance_fee = money.entrance_fee,
                        .fair_fee = simulation::fair_entrance_fee(scene.sim),
                        .old_litter = simulation::old_litter(scene.sim),
                    };
                    for (const entt::entity guest : r.view<const simulation::Thoughts, const simulation::Mood>())
                    {
                        std::vector<std::int32_t> had;
                        for (const simulation::Thought& t : r.get<const simulation::Thoughts>(guest).recent)
                            if (!std::ranges::contains(had, std::to_underlying(t.kind)))
                                had.push_back(std::to_underlying(t.kind));
                        for (const std::int32_t kind : had)
                            ++f.thoughts[kind];
                    }
                    std::vector<std::string_view> foods;
                    for (const auto [e, attraction, shop] : r.view<const simulation::Attraction, const simulation::Shop>().each())
                    {
                        ++f.stalls;
                        f.toilets += shop.product == simulation::Product::Toilet ? 1 : 0;
                        if (shop.product != simulation::Product::Food)
                            continue;
                        ++f.food_stalls;
                        // Kinds of food by the kind of stall selling it.
                        const auto stall = std::ranges::find(scene.rides.stalls, attraction.saved, [](const save::model::Stall& s) { return s.base.id; });
                        if (stall != scene.rides.stalls.end() && !std::ranges::contains(foods, std::string_view(stall->base.symbol)))
                            foods.push_back(stall->base.symbol);
                    }
                    f.food_kinds = static_cast<std::int32_t>(foods.size());
                    for (std::size_t i = 0; i < scene.rides.rides.size(); ++i)
                    {
                        if (scene.rides.rides[i].base.demolished)
                            continue;
                        const save::model::Assessment& a = scene.rides.rides[i].assessment;
                        f.thrilling += a.valid && a.excitement > 5 && a.intensity > 5 ? 1 : 0;
                        const simulation::RideGroup group = i < scene.ride_groups.size() ? scene.ride_groups[i] : simulation::RideGroup::Other;
                        f.coasters += group == simulation::RideGroup::Coaster ? 1 : 0;
                        f.water_rides += group == simulation::RideGroup::Water ? 1 : 0;
                    }
                    for (const auto [e, upkeep] : r.view<const simulation::Upkeep>(entt::exclude<simulation::PoolDoor>).each())
                    {
                        f.downtime.push_back(upkeep.downtime_share() * 100);
                        f.reliability += upkeep.reliability.value * 100;
                        const auto* attraction = r.try_get<const simulation::Attraction>(e);
                        f.running += attraction != nullptr && attraction->open && !attraction->testing && !upkeep.broken ? 1 : 0;
                    }
                    // Rides whose last crash the park still remembers.
                    f.crashed = static_cast<std::int32_t>(std::ranges::count_if(scene.rides.rides, [](const save::model::Ride& ride) { return ride.base.time_since_crash.has_value(); }));
                    f.coaster_dangers = scene.park_model.awards.dangerous_coaster_events.value_or(0);
                    f.pool_dangers = scene.park_model.awards.dangerous_pool_events.value_or(0);
                    const bool listed = scene.build.items.valid() && scene.build.items.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
                    if (listed)
                    {
                        f.themed = simulation::themed_attractions(scene.rides, scene.objects, scene.build.items.get());
                        judge_themes(scene);
                    }
                    const std::span<const rct3::park::BuildItem> catalogue = listed ? std::span<const rct3::park::BuildItem>(scene.build.items.get()) : std::span<const rct3::park::BuildItem> { };
                    for (const auto [e, animal] : r.view<const simulation::Animal>().each())
                    {
                        if (animal.job == simulation::AnimalJob::Crated)
                            continue;
                        // The catalogue lists each kind's adult male, adult female, young male and young female in turn.
                        float rarity = 0;
                        std::size_t nth = (animal.adult ? 0 : 2) + (animal.male ? 0 : 1);
                        for (const rct3::park::BuildItem& item : catalogue)
                            if (item.kind == rct3::park::BuildKind::Animal && strings::equals_ignoring_case(item.symbol, animal.species) && nth-- == 0)
                            {
                                rarity = item.rarity;
                                break;
                            }
                        f.animals.push_back({ .rarity = rarity, .health = animal.health.value, .young = !animal.adult });
                    }
                    if (!f.downtime.empty())
                        f.reliability /= static_cast<float>(f.downtime.size());
                    std::array<bool, 4> kinds { };
                    for (const auto [e, staff] : r.view<const simulation::Staff>().each())
                    {
                        ++f.staff;
                        if (const auto role = std::to_underlying(staff.role); role < kinds.size())
                            kinds[role] = true;
                    }
                    f.every_staff_kind = std::ranges::all_of(kinds, std::identity { });
                    for (const std::int32_t kind : simulation::award_month(scene.park_model.awards, f, scene.gameplay))
                    {
                        const auto slot = static_cast<std::size_t>(kind);
                        if (slot < scene.park_model.awards.prize_money.size())
                            simulation::park::book(scene.park, scene.park_model.finance, scene.ledger_month, units::Money { -scene.park_model.awards.prize_money[slot] }, simulation::park::LedgerLine::PrizeMoney, true);
                        simulation::park::post(scene.park.messages, { .text_key = std::string(simulation::award_message(kind)), .award = kind });
                    }
                }
            }
            // The scenario's goals, checked as each day begins.
            if (const auto day = static_cast<std::int64_t>(std::floor(scene.park.time / (save::model::GameClock::kUnitsPerWeek / 7))); day != scene.goals_day)
            {
                scene.goals_day = day;
                std::vector<simulation::GoalRide> goal_rides;
                for (std::size_t i = 0; i < scene.ride_groups.size() && i < scene.rides.rides.size(); ++i)
                    if (!scene.rides.rides[i].base.demolished)
                        goal_rides.push_back({ scene.ride_groups[i], &scene.rides.rides[i] });
                float health = 0;
                std::int32_t animals = 0;
                for (const auto [e, animal] : scene.sim.registry.view<const simulation::Animal>().each())
                {
                    health += animal.health.value;
                    ++animals;
                }
                // The ledger moved on to this month, so that the period before it is last month's.
                save::model::Finance& money = scene.park_model.finance;
                (void)simulation::park::ledger_month(money, scene.ledger_month, scene.park.date);
                const save::model::LedgerPeriod last_month = money.ledger.size() > 1 ? money.ledger[1] : save::model::LedgerPeriod { };
                const simulation::ParkFigures figures {
                    .park_value = simulation::park::park_value(scene.park.guests, scene.rides).value,
                    .guests = scene.park.guests,
                    .rating = static_cast<std::int32_t>(std::lround(scene.park.rating.value_or(0) * simulation::park::kMaxRating)),
                    .loan = scene.park.loan,
                    .last_month = last_month,
                    .animal_health = animals > 0 ? static_cast<std::int32_t>(std::floor(health / static_cast<float>(animals) * 100)) : 0,
                    .dung = static_cast<std::int32_t>(scene.sim.registry.view<const simulation::Dung>().size()),
                    .rides = goal_rides,
                };
                scene.goal_readings.clear();
                for (const save::model::Condition& c : scene.park_model.objectives.conditions)
                    scene.goal_readings.push_back(simulation::read_goal(c, figures));
                if (const auto won = simulation::check_goals(scene.park_model.objectives, figures, scene.park.time, scene.park.messages))
                {
                    for (const std::int32_t prize : *won)
                    {
                        if (prize == 0)
                            continue;
                        simulation::give_prize(scene.park, scene.park_model.finance, scene.ledger_month, scene.grid, scene.research, prize);
                        scene.prizes.emplace_back(simulation::prize_message(prize));
                        scene.map.stale = true; // Land put on sale shows on the map
                        // A level won, every guest out on the paths jumps for joy.
                        if (prize >= simulation::kApprenticePrize)
                        {
                            ++scene.levels_won;
                            // In a career, the scenario is done at that level.
                            if (!scene.game.career.empty())
                            {
                                const auto status = static_cast<career::ScenarioStatus>(static_cast<int>(career::ScenarioStatus::Apprentice) + prize - simulation::kApprenticePrize);
                                const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                                if (auto changed = career::change_career(
                                        scene.game.career,
                                        [&](career::Career& c) {
                                            if (const auto s = std::ranges::find(c.scenarios, scene.game.scenario, &career::ScenarioProgress::id); s != c.scenarios.end())
                                                career::complete(c, static_cast<std::size_t>(s - c.scenarios.begin()), status, now);
                                        });
                                    !changed)
                                    logging::logger("park").warning("careers: {}", changed.error());
                            }
                            for (const entt::entity guest : scene.sim.registry.view<simulation::Stride, simulation::Mood>())
                                scene.sim.registry.emplace_or_replace<simulation::Gesture>(guest, simulation::GestureKind::Cheer, simulation::kTuning.gestures.shown);
                        }
                    }
                }
            }
            simulation::park::age(scene.park.messages, game_seconds);
            if (scene.simulate)
            {
                // A long frame steps at most a few seconds, so the park catches up rather than stalls.
                constexpr float kMostSeconds = 2;
                scene.sim_time = std::min(scene.sim_time + game_seconds, kMostSeconds);
                scene.sim.registry.ctx().insert_or_assign(simulation::park::guest_weather(scene.park));
                scene.sim.registry.ctx().insert_or_assign(simulation::Daylight { scene.day.time });
                const std::int64_t fee = scene.park_model.has_finance ? scene.park_model.finance.entrance_fee : 0;
                // The pools taken in again once built on, or their rooms opened, closed, priced or given another fluid or
                // other waves.
                std::size_t pools = 14695981039346656037u;
                const auto mix = [&](std::int64_t v) { pools = (pools ^ static_cast<std::size_t>(v)) * 1099511628211u; };
                for (const rct3::park::pools::PoolSquare& s : scene.pools.squares)
                    for (const std::int64_t v : { std::int64_t { s.col }, std::int64_t { s.row }, std::int64_t { std::to_underlying(s.kind) }, std::int64_t { s.surface } })
                        mix(v);
                for (const rct3::park::pools::PoolRoom& room : scene.pools.rooms)
                    for (const std::int64_t v : { std::int64_t { room.col }, std::int64_t { room.row }, std::int64_t { room.side.value }, room.ticket, std::int64_t { room.open }, std::int64_t { room.fluid }, static_cast<std::int64_t>(std::llround(room.waves.height * 100)), static_cast<std::int64_t>(std::llround(room.waves.frequency * 100)), static_cast<std::int64_t>(std::llround(room.waves.on)), static_cast<std::int64_t>(std::llround(room.waves.every)) })
                        mix(v);
                mix(static_cast<std::int64_t>(scene.pools.extras.size() + scene.scenery_pool_extras.size()));
                if (std::exchange(scene.pools_seen, pools) != pools)
                {
                    simulation::import_pools(scene.sim, scene.pools, scene.scenery_pool_extras);
                    scene.machine_waves = rct3::park::pools::machine_waves(scene.pools, scene.scenery_pool_extras, scene.grid);
                }
                scene.sim.registry.ctx().insert_or_assign(simulation::Draw { .entrance_fee = { fee }, .pool_squares = static_cast<std::uint32_t>(scene.footpaths.pools.size()), .rating = scene.park.rating, .guest_cash = { scene.park_model.has_finance ? scene.park_model.finance.average_guest_cash : 0 }, .guest_bank = { scene.park_model.has_finance ? scene.park_model.finance.average_guest_bank_funds : 0 } });
                // What a crashed train's wrecks fly over and into: the ground, and the scenery, stalls and flat rides
                // standing in the park, each as the box it is clicked in.
                // shortcut: wrecks fly through the tracked rides, their own and others', with their stations and
                // fences; upgrade if they should hit other rides' track.
                scene.sim.registry.ctx().insert_or_assign(
                    simulation::WreckSurroundings {
                        .ground = [&scene](glm::vec2 p) { return scene.grid.height_at(p.x, p.y).value_or(0.0f); },
                        .struck =
                            [&scene](glm::vec3 p) {
                                if (scene.pick_boxes_of != scene.objects.size())
                                    place_objects(scene);
                                return std::ranges::any_of(scene.pick_boxes, [&](const rct3::park::object::PickBox& box) {
                                    if (glm::any(glm::lessThan(p, box.park_low)) || glm::any(glm::greaterThan(p, box.park_high)))
                                        return false;
                                    const save::model::PlacedObject& o = scene.objects[box.index];
                                    const auto owner = scene.attractions.find(o.id);
                                    const bool tracked = owner != scene.attractions.end() && owner->second.kind == rct3::park::AttractionRef::Kind::Ride && owner->second.index < scene.rides.rides.size() && scene.rides.rides[owner->second.index].track;
                                    const glm::vec3 local(box.back * glm::vec4(p, 1));
                                    return !o.hidden && !o.fixture && !tracked && glm::all(glm::greaterThanEqual(local, box.low)) && glm::all(glm::lessThanEqual(local, box.high));
                                });
                            },
                    });
                // The animals and their keepers walk the enclosures' tiles.
                const save::model::TerrainGrid& g = scene.grid;
                const auto tile_of = [&g](glm::vec2 p) { return glm::ivec2 { static_cast<int>(std::floor((p.x - g.origin_x) / g.tile_size_x)), static_cast<int>(std::floor((p.y - g.origin_z) / g.tile_size_z)) }; };
                const simulation::AnimalGround animal_ground {
                    .height = [&g](glm::vec2 p) { return g.height_at(p.x, p.y).value_or(0.0f); },
                    .enclosure =
                        [&](glm::vec2 p) {
                            const glm::ivec2 t = tile_of(p);
                            return scene.enclosures.at(t.x, t.y);
                        },
                    .somewhere = [&](std::int32_t which, random::RandomState& dice) -> std::optional<glm::vec2> {
                        const auto tiles = static_cast<std::uint64_t>(rct3::park::enclosure_size(scene.enclosures, which));
                        if (tiles == 0)
                            return std::nullopt;
                        for (std::uint64_t i = 0, pick = random::below(dice, tiles); i < scene.enclosures.tiles.size(); ++i)
                            if (scene.enclosures.tiles[i] == which && pick-- == 0)
                            {
                                const auto col = static_cast<float>(i % static_cast<std::size_t>(scene.enclosures.cols)) + static_cast<float>(random::unit(dice));
                                const auto row = static_cast<float>(i / static_cast<std::size_t>(scene.enclosures.cols)) + static_cast<float>(random::unit(dice));
                                return glm::vec2 { g.origin_x + col * g.tile_size_x, g.origin_z + row * g.tile_size_z };
                            }
                        return std::nullopt;
                    },
                };
                profiling::section("Guests", [&] {
                    for (; scene.sim_time >= simulation::kTickSeconds; scene.sim_time -= simulation::kTickSeconds)
                    {
                        simulation::step(scene.sim);
                        simulation::step_animals(scene.sim, animal_ground, { simulation::kTickSeconds }, scene.park.messages);
                    }
                });
                // A coaster's lap just run becomes its graphs.
                for (std::size_t i = 0; i < scene.ride_entities.size() && i < scene.rides.rides.size(); ++i)
                    if (auto* fleet = scene.ride_entities[i] == entt::null ? nullptr : scene.sim.registry.try_get<simulation::Trains>(scene.ride_entities[i]); fleet != nullptr && fleet->lap_done)
                    {
                        scene.rides.rides[i].graphs = std::move(*fleet->lap_done);
                        fleet->lap_done.reset();
                    }
                // The scenario's VIPs: told of, let in, looking round and judging the park as they go.
                std::map<std::string_view, std::int32_t, std::less<>> theme_of; // Of each themed item, by symbol
                const auto themed = [&](glm::vec3 at, std::int32_t theme, std::int32_t reach) {
                    if (theme_of.empty() && scene.build.items.valid() && scene.build.items.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                        for (const rct3::park::BuildItem& item : scene.build.items.get())
                            if (item.style_index >= 0)
                                theme_of.try_emplace(item.symbol, item.style_index);
                    const save::model::TerrainGrid& g = scene.grid;
                    if (g.tile_size_x <= 0 || g.tile_size_z <= 0)
                        return 0;
                    const glm::ivec2 tile { static_cast<int>(std::floor((at.x - g.origin_x) / g.tile_size_x)), static_cast<int>(std::floor((at.z - g.origin_z) / g.tile_size_z)) };
                    // shortcut: the theme numbers of a VIP goal are taken to be the build catalogue's; check them
                    // against a level whose VIP wants a theme if themed areas are never found.
                    return static_cast<std::int32_t>(std::ranges::count_if(scene.objects, [&](const save::model::PlacedObject& o) {
                        const auto found = theme_of.find(o.symbol);
                        return found != theme_of.end() && found->second == theme && std::abs(o.col - tile.x) <= reach && std::abs(o.row - tile.y) <= reach;
                    }));
                };
                const auto name = [&](std::uint8_t who) { return strings::to_utf8(assets.strings->get(simulation::vip_name_key(who))); };
                simulation::advance_vips(scene.sim, scene.park_model.objectives, scene.park.time, game_seconds, scene.park.messages, { .name = name, .themed = themed });
                // The guests in the park now, coming in at the gate and going home, for the bar and the rating.
                const auto inside = scene.sim.registry.view<const simulation::Visitor>(entt::exclude<simulation::Left>);
                scene.park.guests = static_cast<std::int32_t>(std::ranges::distance(inside.begin(), inside.end()));
                // Each thing broken costs the park some rating, and bad runs of it are told to the player.
                if (const auto* vandalism = scene.sim.registry.ctx().find<simulation::Vandalism>())
                {
                    const auto broken = static_cast<float>(vandalism->broken - scene.vandalism_seen);
                    if (scene.park.rating)
                        *scene.park.rating = std::max(0.0f, *scene.park.rating - kVandalismPenalty * broken / static_cast<float>(simulation::park::kMaxRating));
                    scene.vandalism_seen = vandalism->broken;
                    for (; scene.vandalism_warnings_seen < vandalism->warnings; ++scene.vandalism_warnings_seen)
                        simulation::park::post(scene.park.messages, { .text_key = "Thought_PI_Park_Vandalism1" });
                }
                if (!scene.themes_judged && scene.build.items.valid() && scene.build.items.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                    judge_themes(scene);
                // What the park's inspector found wrong, each told about the attraction or worker they found it at,
                // or about the guests.
                if (auto* findings = scene.sim.registry.ctx().find<simulation::Findings>())
                {
                    using enum simulation::Finding;
                    constexpr std::array<std::string_view, 26> kKeys { // By Finding
                                                                       "Thought_PI_Stall_TooExpensive1",
                                                                       "Thought_PI_Stall_TooCheap1",
                                                                       "Thought_PI_Ride_LongQueueTime1",
                                                                       "Thought_PI_Ride_NoPathToEntrance1",
                                                                       "Thought_PI_General_NoMaintenanceManNear1",
                                                                       "Thought_PI_Vista_InappropriateStyle1",
                                                                       "Thought_PI_Staff_Lazy1",
                                                                       "Thought_PI_Staff_Unhappy1",
                                                                       "Thought_PI_PoolDirty1",
                                                                       "Thought_PI_PoolVeryDirty1",
                                                                       "Thought_PI_Guests_Hungry1",
                                                                       "Thought_PI_Guests_Thirsty1",
                                                                       "Thought_PI_Guests_Tired1",
                                                                       "Thought_PI_Guests_Toilet1",
                                                                       "Thought_PI_NoMechanicsCanReachRide1",
                                                                       "Thought_PI_MechanicBadZones1",
                                                                       "Thought_PI_Park_Breakdowns1",
                                                                       "Thought_PI_Staff_NoPathToGetTo1",
                                                                       "Thought_PI_NoKeepersCanReachShelter1",
                                                                       "Thought_PI_AnimalKeepersBadZones1",
                                                                       "Thought_PI_AnimalHungryFoodTroughsEmpty1",
                                                                       "Thought_PI_TooMuchDung1",
                                                                       "Thought_PI_DangerousFlume1",
                                                                       "Thought_PI_DangerousDivingBoard1",
                                                                       "StaffMessage_WantsToResign",
                                                                       "StaffMessage_Resigned"
                    };
                    std::vector<entt::entity> quitting;
                    const auto& r = scene.sim.registry;
                    for (const auto& [finding, at] : findings->found)
                    {
                        simulation::park::ParkMessage message { .text_key = std::string(kKeys[std::to_underlying(finding)]) };
                        if (const auto* attraction = r.valid(at) ? r.try_get<simulation::Attraction>(at) : nullptr)
                        {
                            const bool stall = r.all_of<simulation::Shop>(at);
                            if (finding == NoPath && stall)
                                message.text_key = "Thought_PI_Stall_NoPathToGetTo1";
                            if (attraction->saved != 0)
                                message.subject = attraction->saved;
                            // A pool's changing room, by the pool complex's name.
                            if (const auto* door = r.try_get<simulation::PoolDoor>(at))
                                if (const auto room = std::ranges::find_if(scene.pools.rooms, [&](const auto& room) { return room.col == door->col && room.row == door->row; }); room != scene.pools.rooms.end())
                                    message.subject_name = room->complex_name;
                            message.panel = ui::screens::info::message_panel(finding == TooDear || finding == TooCheap ? ui::screens::info::AttractionTab::Items : (finding == NoMechanic || finding == NoMechanicCanReach || finding == BadZones) && !stall ? ui::screens::info::AttractionTab::Maintenance : ui::screens::info::AttractionTab::Mode);
                        }
                        // shortcut: a worker is named but cannot be gone to from the message; add it once messages open people.
                        else if (const auto shown = std::ranges::find(scene.shown_people, at, &ShownPerson::guest); r.valid(at) && shown != scene.shown_people.end() && shown->staff && shown->saved < scene.people.staff.size())
                        {
                            message.subject_name = strings::to_utf8(ui::screens::info::person_name({ .staff = &scene.people.staff[shown->saved] }, *assets.strings));
                            if (finding == Quit)
                                quitting.push_back(at);
                        }
                        // An animal by its name, an animal house by its scenery item's.
                        else if (const auto* animal = r.valid(at) ? r.try_get<simulation::Animal>(at) : nullptr)
                            message.subject_name = animal->name;
                        else if (const auto* house = r.valid(at) ? r.try_get<simulation::AnimalHouse>(at) : nullptr)
                        {
                            const auto placed = std::ranges::find(scene.objects, house->object, &save::model::PlacedObject::id);
                            if (placed != scene.objects.end() && scene.build.items.valid() && scene.build.items.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                                if (const auto item = std::ranges::find(scene.build.items.get(), placed->symbol, &rct3::park::BuildItem::symbol); item != scene.build.items.get().end())
                                    message.subject_name = strings::to_utf8(assets.strings->get(item->name_text));
                        }
                        else if (r.valid(at))
                            continue; // Gone since
                        simulation::park::post(scene.park.messages, std::move(message));
                    }
                    findings->found.clear();
                    // A worker who quits goes as one sacked does, their window with them.
                    for (const entt::entity e : quitting)
                        if (const auto shown = std::ranges::find(scene.shown_people, e, &ShownPerson::guest); shown != scene.shown_people.end())
                        {
                            const auto i = static_cast<std::size_t>(shown - scene.shown_people.begin());
                            if (auto panel = scene.desktop->root().find_as<ui::screens::info::InfoPanel>(kInfoName); panel && scene.shown_person == i)
                                panel->hide();
                            fire(scene, i);
                        }
                }
                // One lifeguard sits in each lifeguard chair, come with it and gone with it, as the game has them.
                // shortcut: a sacked lifeguard's chair gets a new one when the chairs next change; match the game once
                // what it does with the empty chair is known.
                if (scene.chairs_changed && scene.anchors.size() == scene.objects.size() && scene.places.transforms.size() == scene.objects.size() && scene.build.items.valid() && scene.build.items.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                {
                    scene.chairs_changed = false;
                    std::unordered_set<std::string_view> chair_symbols;
                    std::unordered_map<std::string_view, rct3::park::pools::PoolExtraFit> extra_fits;
                    for (const rct3::park::BuildItem& item : scene.build.items.get())
                        if (!item.pool_extra.empty())
                        {
                            const rct3::park::pools::PoolExtraFit fit = rct3::park::pools::pool_extra_fit(item.pool_extra);
                            extra_fits.emplace(item.symbol, fit);
                            if (fit.lifeguard)
                                chair_symbols.insert(item.symbol);
                        }
                    // The extras a park from the game placed stand as scenery, not in `pools`; its swimmers use them too.
                    scene.scenery_pool_extras.clear();
                    for (const save::model::PlacedObject& o : scene.objects)
                        if (const auto fit = extra_fits.find(o.symbol); fit != extra_fits.end() && !o.hidden && !std::ranges::contains(scene.pool_pieces, o.id))
                            scene.scenery_pool_extras.push_back({ .symbol = o.symbol, .overlay = o.overlay, .col = o.col, .row = o.row, .side = rct3::park::ModelTurn::from(o.rotation).facing(), .fit = fit->second });
                    const save::model::TerrainGrid& g = scene.grid;
                    const auto tile_of = [&g](glm::vec3 p) { return glm::ivec2 { static_cast<int>(std::floor((p.x - g.origin_x) / g.tile_size_x)), static_cast<int>(std::floor((p.z - g.origin_z) / g.tile_size_z)) }; };
                    std::vector<std::size_t> chairs;
                    for (std::size_t i = 0; i < scene.objects.size(); ++i)
                        if (!scene.objects[i].hidden && chair_symbols.contains(scene.objects[i].symbol))
                            chairs.push_back(i);
                    auto& r = scene.sim.registry;
                    std::vector<entt::entity> lifeguards;
                    for (const auto [e, staff] : r.view<const simulation::Staff>(entt::exclude<simulation::Stride>).each())
                        if (staff.role == simulation::StaffRole::Other)
                            lifeguards.push_back(e);
                    const auto seated = [&](entt::entity e, std::size_t chair) { return tile_of(r.get<simulation::Pose>(e).position) == glm::ivec2 { scene.objects[chair].col, scene.objects[chair].row }; };
                    for (const entt::entity e : lifeguards)
                    {
                        if (std::ranges::any_of(chairs, [&](std::size_t c) { return seated(e, c); }))
                            continue;
                        if (const auto shown = std::ranges::find(scene.shown_people, e, &ShownPerson::guest); shown != scene.shown_people.end())
                        {
                            const auto i = static_cast<std::size_t>(shown - scene.shown_people.begin());
                            if (auto panel = scene.desktop->root().find_as<ui::screens::info::InfoPanel>(kInfoName); panel && scene.shown_person == i)
                                panel->hide();
                            fire(scene, i);
                        }
                        else
                            r.destroy(e);
                    }
                    // Saved lifeguards too: their saved heading is wherever they were looking about at the time.
                    for (const std::size_t c : chairs)
                    {
                        // Facing out over the front of the chair, which its model has along -Z.
                        const glm::mat4& m = scene.places.transforms[c];
                        const units::Radians facing { std::atan2(-m[2][0], -m[2][2]) };
                        if (const auto sitter = std::ranges::find_if(lifeguards, [&](entt::entity e) { return r.valid(e) && seated(e, c); }); sitter != lifeguards.end())
                        {
                            auto& pose = r.get<simulation::Pose>(*sitter);
                            pose.heading = pose.was_heading = facing;
                        }
                        else
                            (void)simulation::add_lifeguard(scene.sim, scene.anchors[c], facing);
                    }
                }
                // What is broken looks it, and looks whole again once mended.
                rct3::park::object::Broken broken;
                for (const auto [e, item] : scene.sim.registry.view<const simulation::PathItem>().each())
                    if (item.broken && item.object != 0)
                        broken.insert(item.object);
                if (broken != scene.broken_objects)
                {
                    scene.broken_objects = std::move(broken);
                    objects_changed(scene);
                }
                // Photos of the player's guests at photo spots: from in front of them, framing their party.
                if (auto* photos = scene.sim.registry.ctx().find<simulation::Photos>())
                {
                    const auto& r = scene.sim.registry;
                    const bool wanted = options.flag(config::game::GameOption::AllPhotos) && options.flag(config::game::GameOption::PhotoSpotPhotos);
                    for (const simulation::Photo& photo : std::exchange(photos->taken, { }))
                    {
                        // shortcut: photos past a few waiting are dropped, rather than stalling frame after frame; queue them over time if parks take many at once.
                        if (!wanted || !r.valid(photo.guest) || scene.photos.size() >= 4)
                            continue;
                        std::vector<glm::vec3> points;
                        const auto frame = [&](entt::entity e) {
                            const glm::vec3 at = r.get<simulation::Pose>(e).position;
                            points.push_back(at);
                            points.push_back(at + glm::vec3(0, kPhotoPersonTall, 0));
                        };
                        if (const auto* member = r.try_get<simulation::Member>(photo.guest))
                            std::ranges::for_each(r.get<simulation::Party>(member->party).members, frame);
                        else
                            frame(photo.guest);
                        const float yaw = r.get<simulation::Pose>(photo.guest).heading.value + glm::pi<float>();
                        const std::string& name = r.get<simulation::Visitor>(photo.guest).name;
                        scene.photos.push_back({ rct3::park::framed(points, yaw, kPhotoPitch.value, kPhotoClosest), std::format("{} at photo spot_", name.empty() ? "Guest" : name) });
                    }
                }
                // Their money and mood into the saved guests, for their windows and the guest list to show.
                for (const ShownPerson& p : scene.shown_people)
                {
                    if (p.staff || p.saved >= scene.people.guests.size() || !scene.sim.registry.valid(p.guest))
                        continue;
                    save::model::Guest& kept = scene.people.guests[p.saved];
                    if (const auto* wallet = scene.sim.registry.try_get<simulation::Wallet>(p.guest))
                        kept.cash = parkfile::kept_cash(*wallet);
                    if (const auto* mood = scene.sim.registry.try_get<simulation::Mood>(p.guest))
                    {
                        kept.mood_now.happiness = mood->happiness.value;
                        kept.mood_now.energy = mood->energy.value;
                        kept.mood_now.nausea = mood->nausea.value;
                    }
                }
                // New thoughts go into the people's saved thoughts, newest first, for their windows to show.
                for (ShownPerson& p : scene.shown_people)
                {
                    const auto* thoughts = p.guest == entt::null || !scene.sim.registry.valid(p.guest) || p.saved >= (p.staff ? scene.people.staff.size() : scene.people.guests.size()) ? nullptr : scene.sim.registry.try_get<simulation::Thoughts>(p.guest);
                    if (thoughts == nullptr || thoughts->had == p.thoughts_seen)
                        continue;
                    save::model::Person& person = p.staff ? scene.people.staff[p.saved].person : scene.people.guests[p.saved].person;
                    auto& slots = person.thoughts;
                    for (std::size_t k = std::min<std::size_t>(thoughts->had - p.thoughts_seen, thoughts->recent.size()); k-- > 0;)
                    {
                        const simulation::Thought& t = thoughts->recent[k];
                        const auto newest = std::ranges::max(slots, { }, &save::model::Thought::time).time;
                        const auto* about = scene.sim.registry.valid(t.about) ? scene.sim.registry.try_get<simulation::Attraction>(t.about) : nullptr;
                        std::shift_right(slots.begin(), slots.end(), 1);
                        // A thought about a path item is about the placed object it stands as.
                        const auto* item = about == nullptr && scene.sim.registry.valid(t.about) ? scene.sim.registry.try_get<simulation::PathItem>(t.about) : nullptr;
                        slots[0] = { .about = { about != nullptr ? about->saved : item != nullptr ? item->object : 0 }, .id = std::to_underlying(t.kind), .time = newest ? *newest + 1 : 0.0f, .variant = static_cast<std::int32_t>(t.variant) };
                        // shortcut: the message names what they thought about but cannot take the view to the
                        // person; add it once messages open people.
                        if (std::string key = ui::thought_text_key(*assets.strings, slots[0].id, slots[0].variant); person.thoughts_to_console && !key.empty())
                            simulation::park::post(scene.park.messages, { .text_key = std::move(key), .subject = about != nullptr && about->saved != 0 ? std::optional(about->saved) : std::nullopt });
                    }
                    p.thoughts_seen = thoughts->had;
                }
                // Staff are paid from the park's cash, and the player hears of rides breaking down and being fixed.
                if (const auto* payroll = scene.sim.registry.ctx().find<simulation::Payroll>())
                {
                    simulation::park::book(scene.park, scene.park_model.finance, scene.ledger_month, units::Money { payroll->paid.value - scene.wages_seen }, simulation::park::LedgerLine::StaffWages, true);
                    scene.wages_seen = payroll->paid.value;
                }
                // So are pools' cleaning fluids and stalls' stock, and what guests pay for rides, shops and entry
                // goes into it, each on its own line. A line nothing moved on stays as it was, unshown if the park
                // file never had it.
                const auto* supplies = scene.sim.registry.ctx().find<simulation::Supplies>();
                const auto* receipts = scene.sim.registry.ctx().find<simulation::Receipts>();
                for (std::size_t line = 0; line < simulation::park::kLedgerLines; ++line)
                    if (const std::int64_t cost = (supplies ? supplies->paid[line].value - scene.supplies_seen.paid[line].value : 0) - (receipts ? receipts->paid[line].value - scene.receipts_seen.paid[line].value : 0); cost != 0)
                        simulation::park::book(scene.park, scene.park_model.finance, scene.ledger_month, units::Money { cost }, static_cast<simulation::park::LedgerLine>(line), true);
                if (supplies)
                    scene.supplies_seen = *supplies;
                if (receipts)
                    scene.receipts_seen = *receipts;
                // The loan's interest and the open rides' running costs come out of it whether it can pay or not.
                {
                    auto& charges = scene.sim.registry.ctx().emplace<simulation::Charges>();
                    charges.loan = scene.park.loan;
                    charges.rate = scene.park_model.finance.interest_rate_percent;
                    simulation::park::book(scene.park, scene.park_model.finance, scene.ledger_month, std::exchange(charges.interest, { }), simulation::park::LedgerLine::LoanInterest, true);
                    simulation::park::book(scene.park, scene.park_model.finance, scene.ledger_month, std::exchange(charges.running, { }), simulation::park::LedgerLine::RideRunningCosts, true);
                }
                // The staff's mood into the saved staff, for their windows to show.
                for (const ShownPerson& p : scene.shown_people)
                    if (const auto* staff = p.staff && p.saved < scene.people.staff.size() && scene.sim.registry.valid(p.guest) ? scene.sim.registry.try_get<simulation::Staff>(p.guest) : nullptr)
                        copy_mood(*staff, scene.people.staff[p.saved]);
                // The rides' upkeep into the saved rides, for their windows to show.
                for (std::size_t i = 0; i < scene.ride_entities.size() && i < scene.rides.rides.size(); ++i)
                {
                    const auto* upkeep = scene.ride_entities[i] != entt::null ? scene.sim.registry.try_get<simulation::Upkeep>(scene.ride_entities[i]) : nullptr;
                    if (upkeep == nullptr)
                        continue;
                    save::model::Ride& ride = scene.rides.rides[i];
                    ride.breakdown.reliability = upkeep->reliability.value * 100;
                    ride.breakdown.current = static_cast<save::model::BreakdownKind>(std::to_underlying(upkeep->kind));
                    ride.breakdown.last = static_cast<save::model::BreakdownKind>(std::to_underlying(upkeep->last));
                    ride.base.time_since_inspection = upkeep->since_inspection;
                    ride.breakdown.downtime_windows = upkeep->downtime;
                    ride.breakdown.saved_downtime_percent.reset();
                    using enum save::model::RepairStatus;
                    ride.base.repair_status = !upkeep->broken ? Fine : upkeep->mechanic == entt::null ? MechanicCalled : MechanicOnTheWay;
                    ride.base.mechanic_asked = upkeep->called;
                    ride.base.flags = upkeep->broken ? ride.base.flags | save::model::AttractionCommon::kBrokenDown : ride.base.flags & ~save::model::AttractionCommon::kBrokenDown;
                }
                // A crash closes its ride, counts as dangerous for the awards, and is remembered a while.
                for (std::size_t i = 0; i < scene.ride_entities.size() && i < scene.rides.rides.size(); ++i)
                {
                    const auto* wrecks = scene.ride_entities[i] != entt::null ? scene.sim.registry.try_get<simulation::Wrecks>(scene.ride_entities[i]) : nullptr;
                    if (wrecks == nullptr)
                        continue;
                    save::model::Ride& ride = scene.rides.rides[i];
                    ride.base.time_since_crash = wrecks->since;
                    // Each wreck goes up in flames and smoke as it blows up.
                    for (std::size_t t = 0; t < wrecks->trains.size(); ++t)
                        for (std::size_t k = 0; k < wrecks->trains[t].size(); ++k)
                        {
                            const simulation::Wreck& wreck = wrecks->trains[t][k];
                            const auto key = std::tuple { scene.ride_entities[i], t, k };
                            if (wreck.state == simulation::WreckState::Flying)
                                scene.wrecks_blown.erase(key);
                            else if (wreck.state == simulation::WreckState::Burning && scene.wrecks_blown.insert(key).second)
                                for (const std::string_view effect : { "CoasterExplosion", "CoasterSmoke" })
                                    fire_park_particles(scene.particles, effect, wreck.at, glm::vec3(0, 1, 0));
                        }
                    auto& [crashes, dangers, injured] = scene.crashes_seen[scene.ride_entities[i]];
                    if (wrecks->crashes == crashes)
                        continue;
                    crashes = wrecks->crashes;
                    const std::uint32_t hurt = wrecks->injured - std::exchange(injured, wrecks->injured);
                    simulation::park::post(scene.park.messages, { .text_key = hurt > 0 ? "Thought_PI_Ride_Crashed_Injuries1" : "Thought_PI_Ride_Crashed1", .subject = ride.base.id, .number = hurt });
                    ride.base.state = save::model::AttractionState::Closed;
                    scene.shown_closed = scene.shown_closed || (scene.shown && scene.shown->kind == rct3::park::AttractionRef::Kind::Ride && scene.shown->index == i);
                    auto& events = scene.park_model.awards.dangerous_coaster_events;
                    events = events.value_or(0) + static_cast<std::int32_t>(wrecks->dangers - dangers);
                    dangers = wrecks->dangers;
                }
                // Each hard landing off a pool slide or a diving board is dangerous for the awards too.
                if (auto* pool = scene.sim.registry.ctx().find<simulation::PoolDangers>(); pool != nullptr && pool->landings > 0)
                {
                    auto& events = scene.park_model.awards.dangerous_pool_events;
                    events = events.value_or(0) + static_cast<std::int32_t>(std::exchange(pool->landings, 0));
                }
                for (const auto [e, upkeep, attraction] : scene.sim.registry.view<const simulation::Upkeep, const simulation::Attraction>().each())
                {
                    const auto at = std::ranges::find(scene.ride_entities, e);
                    const auto index = static_cast<std::size_t>(at - scene.ride_entities.begin());
                    // Stalls and cash machines break with time and have no maintenance page: their messages open
                    // the window, and the window shows the state.
                    const bool stall = upkeep.break_in.value >= 0;
                    const auto tell = [&](std::string_view key) {
                        if (stall)
                            simulation::park::post(scene.park.messages, { .text_key = std::string(key), .subject = attraction.saved != 0 ? std::optional(attraction.saved) : std::nullopt, .panel = ui::screens::info::message_panel(ui::screens::info::AttractionTab::View) }); // Named once shown, as the window names it
                        else
                            simulation::park::post(scene.park.messages, { .text_key = std::string(key), .subject = index < scene.rides.rides.size() ? std::optional(scene.rides.rides[index].base.id) : std::nullopt, .subject_name = attraction.name, .panel = ui::screens::info::message_panel(ui::screens::info::AttractionTab::Maintenance), .field_key = std::string(ui::screens::info::breakdown_key(static_cast<save::model::BreakdownKind>(std::to_underlying(upkeep.kind)))) });
                    };
                    const auto* shop = scene.sim.registry.try_get<simulation::Shop>(e);
                    const bool cash = shop != nullptr && shop->product == simulation::Product::CashMachine;
                    const auto unreliable = std::ranges::find(scene.unreliable_rides, e);
                    if ((upkeep.reliability.value < kLowReliability) != (unreliable != scene.unreliable_rides.end()))
                    {
                        if (unreliable == scene.unreliable_rides.end())
                        {
                            scene.unreliable_rides.push_back(e);
                            tell("RideReliabilityLow");
                        }
                        else
                            scene.unreliable_rides.erase(unreliable);
                    }
                    const auto told = std::ranges::find(scene.broken_rides, e, &BrokenRide::ride);
                    if (upkeep.broken && told != scene.broken_rides.end() && (told->unrepaired += game_seconds) >= kStillBrokenReminder)
                    {
                        told->unrepaired = 0;
                        tell(!stall ? "RideStillBrokenMessage" : cash ? "CashMachineStillBrokenMessage" : "FoodStallStillBrokenMessage");
                    }
                    // A broken stall's or cash machine's state into the saved one, for its window to show.
                    if (stall && (upkeep.broken || told != scene.broken_rides.end()))
                    {
                        const auto mine = [&](const save::model::AttractionCommon& base) { return base.id == attraction.saved; };
                        const auto s = std::ranges::find_if(scene.rides.stalls, mine, &save::model::Stall::base);
                        const auto c = std::ranges::find_if(scene.rides.rides, mine, &save::model::Ride::base);
                        if (save::model::AttractionCommon* base = s != scene.rides.stalls.end() ? &s->base : c != scene.rides.rides.end() ? &c->base : nullptr)
                        {
                            using enum save::model::RepairStatus;
                            base->repair_status = !upkeep.broken ? Fine : upkeep.mechanic == entt::null ? MechanicCalled : MechanicOnTheWay;
                            base->flags = upkeep.broken ? base->flags | save::model::AttractionCommon::kBrokenDown : base->flags & ~save::model::AttractionCommon::kBrokenDown;
                        }
                    }
                    if (upkeep.broken == (told != scene.broken_rides.end()))
                        continue;
                    if (upkeep.broken)
                        scene.broken_rides.push_back({ e });
                    else
                        scene.broken_rides.erase(told);
                    // Its window, if open, shows it broken down or mended.
                    if (auto panel = scene.desktop->root().find_as<ui::screens::info::InfoPanel>(kInfoName); panel && panel->visible && scene.shown && scene.shown == attraction_with(scene, attraction.saved))
                        show_info_page(scene, *panel, assets, options);
                    if (stall)
                    {
                        tell(!upkeep.broken ? "RideFixedMessage" : cash ? "CashMachineBrokenMessage" : "FoodStallBrokenMessage");
                        continue;
                    }
                    // Each kind of breakdown has its own message, in the order the kinds are numbered.
                    constexpr std::array<std::string_view, 9> kBroken = { "RideBrokenMessage", "RideBrokenMessageEmergencyStop", "RideBrokenMessageRestraintsFailOpen", "RideBrokenMessageRestraintsFailClose", "RideBrokenMessageDoorsFailOpen", "RideBrokenMessageDoorsFailClose", "RideBrokenMessageStuckLeavingStation", "RideBrokenMessageBrakesFail", "RideBrokenMessageControlFail" };
                    const auto kind = static_cast<std::size_t>(std::to_underlying(upkeep.kind));
                    tell(!upkeep.broken ? "RideFixedMessage" : kind < kBroken.size() ? kBroken[kind] : kBroken[0]);
                    // A broken ride is taped off and gets its own fence back once fixed: the tape panels stand where
                    // the fence's do, so each panel only changes its model.
                    if (index >= scene.rides.rides.size())
                        continue;
                    save::model::Ride& ride = scene.rides.rides[index];
                    ride.override_fence_symbol = upkeep.broken ? kTapeFence : "";
                    ride.override_fence_overlay = upkeep.broken ? kTapeFenceOverlay : "";
                    const save::model::PlacedObject look { .symbol = ride.override_fence_symbol.empty() ? ride.fence_symbol : ride.override_fence_symbol, .overlay = ride.override_fence_symbol.empty() ? ride.fence_overlay : ride.override_fence_overlay };
                    // shortcut: a ride with no fence of its own stays unfenced while broken; add panels for it if
                    // such rides turn up outside old saves.
                    const auto fence = scene.ride_fence_pieces.find(ride.base.id);
                    if (fence == scene.ride_fence_pieces.end() || fence->second.empty() || (!look.symbol.empty() && !load_object_model(scene, scene.placing.models, assets, look)))
                        continue;
                    wait_for_build(scene);
                    for (save::model::PlacedObject& panel : scene.objects)
                    {
                        if (!std::ranges::contains(fence->second, panel.id))
                            continue;
                        panel.hidden = look.symbol.empty(); // Fixed, with no fence of its own
                        if (look.symbol.empty())
                            continue;
                        panel.symbol = look.symbol;
                        panel.overlay = look.overlay;
                    }
                    objects_changed(scene);
                }
            }
            profiling::section("Cars", [&] { place_cars(scene, viewer, rct3::park::view_projection(scene.camera, aspect(device), scene.seat, scene.seat_across), elapsed); });
        });
        profiling::section("Daylight", [&] { show_time_of_day(scene, viewer); });

        const profiling::Scope zone("Bar");
        if (bar)
        {
            ui::screens::park_view::ParkBarUnits units;
            units.currency = ui::currency(*assets.strings, options.choice(config::game::GameOption::Currency));
            units.fahrenheit = options.choice(config::game::GameOption::TemperatureUnits) == kFahrenheit;
            // The game writes its times with am and pm in English only.
            units.twelve_hour = assets.fonts.language == ui::kDefaultLanguage;
            const auto& ledger = scene.park_model.finance.ledger;
            ui::screens::park_view::ParkBarValues values = ui::screens::park_view::park_bar_values(scene.park, scene.day.time, *assets.strings, units, ledger.size() > 1 ? std::optional(-ledger[1].raw_sum()) : std::nullopt);
            // The player chooses how the day goes in a sandbox, whatever its money.
            if (scene.game.sandbox)
                values.day_mode = scene.day.mode;
            bar->show(values);
        }
        if (auto messages = desktop.root().find_as<ui::screens::park_view::MessageDrawer>(kMessagesName))
        {
            // shortcut: only rides and stalls can be gone to, not people or path items; add them when a message names one.
            // Saved parks keep messages without their subject's name, so it is filled in from the attraction.
            for (simulation::park::ParkMessage& m : scene.park.messages.list)
            {
                const auto ref = m.subject && m.subject_name.empty() ? attraction_with(scene, *m.subject) : std::nullopt;
                if (ref)
                    m.subject_name = strings::to_utf8(ui::screens::info::attraction_name(ref->kind == rct3::park::AttractionRef::Kind::Stall ? scene.rides.stalls[ref->index].base : scene.rides.rides[ref->index].base, *assets.strings));
            }
            std::vector<ui::screens::park_view::DrawerMessage> shown = ui::screens::park_view::drawer_messages(scene.park.messages, *assets.strings, messages->style());
            // A message about an attraction shows the attraction's own icon, the one it has in the build lists, and
            // the camera when it has none.
            const auto items = scene.build.items.valid() && scene.build.items.wait_for(std::chrono::seconds(0)) == std::future_status::ready ? std::span(scene.build.items.get()) : std::span<const rct3::park::BuildItem> { };
            for (std::size_t i = 0; i < shown.size(); ++i)
            {
                // An invention shows the item's icon and leads to it in its build list.
                if (const auto& b = scene.park.messages.list[i].build_item; b && *b < items.size())
                {
                    if (!items[*b].icon.empty())
                        shown[i].icon = items[*b].icon;
                    shown[i].clickable = true;
                    continue;
                }
                const auto& subject = scene.park.messages.list[i].subject;
                const auto ref = subject ? attraction_with(scene, *subject) : std::nullopt;
                if (!ref)
                    continue;
                std::string overlay = ref->kind == rct3::park::AttractionRef::Kind::Stall ? scene.rides.stalls[ref->index].base.overlay_path : scene.rides.rides[ref->index].base.overlay_path;
                std::ranges::replace(overlay, '\\', '/');
                const auto item = std::ranges::find_if(items, [&](const rct3::park::BuildItem& it) { return !it.icon.empty() && strings::equals_ignoring_case(it.overlay, overlay); });
                if (item != items.end())
                    shown[i].icon = item->icon;
                shown[i].clickable = true;
            }
            messages->show(shown);
            scene.message_alert = scene.message_alert || messages->take_alert();
            const auto serial = messages->take_clicked();
            const auto message = serial ? std::ranges::find(scene.park.messages.list, *serial, &simulation::park::ParkMessage::serial) : scene.park.messages.list.end();
            // An award's icon opens the park's awards, as the game does.
            if (message != scene.park.messages.list.end() && message->award)
                open_management_window(desktop, ui::screens::management::ManagementTopic::ParkOperations, ui::screens::management::kAwardsTab, management_facts(scene), *assets.strings, assets.palette, info_units(assets, options));
            // An invention's opens its build list with it marked, from the menu item that opens that list.
            else if (const auto tools = desktop.root().find_as<ui::screens::park_view::ToolRow>(kToolsName); message != scene.park.messages.list.end() && message->build_item && *message->build_item < items.size() && tools)
            {
                for (auto t = std::to_underlying(ui::screens::park_view::ToolItem::DeleteOne); t <= std::to_underlying(ui::screens::park_view::ToolItem::GuestPayments); ++t)
                {
                    const auto item = static_cast<ui::screens::park_view::ToolItem>(t);
                    if (ui::screens::build::build_list(item) == items[*message->build_item].list && open_build_list(scene.build, desktop.root(), item, tools->item(item), *tools, *assets.strings, assets.palette, ui::currency(*assets.strings, options.choice(config::game::GameOption::Currency)), message->build_item))
                        break;
                }
            }
            // An attraction's takes the view to it and opens the info panel on it.
            else if (const auto ref = message != scene.park.messages.list.end() && message->subject ? attraction_with(scene, *message->subject) : std::nullopt; ref && info)
            {
                // At the tab the message names; one the attraction lacks opens its view.
                open_info(scene, *info, *ref, assets, options, ui::screens::info::message_tab(message->panel.value_or(-1)));
                // The middle of the whole attraction, the same on every click, as in the game.
                const glm::vec3 at = scene.view_camera.target;
                // Close enough to make it out, never further out than the player was.
                scene.camera.target = { at.x, scene.camera.target.y, at.z };
                scene.camera.distance = std::min(scene.camera.distance, std::max(scene.view_camera.distance * kFocusZoomOut, kFocusClosest));
                scene.camera = rct3::park::on_ground(scene.camera, scene.grid);
                // It shows in the middle of what the drawer leaves of the screen, not behind the drawer: the view
                // slides on along the ground until it does, found by the secant method from no slide.
                const ui::Rect screen = desktop.root().rect();
                const float want = (messages->rect().y1 + screen.y1) / 2;
                const auto off = [&](float slide) {
                    const glm::vec4 clip = rct3::park::view_projection(rct3::park::panned(scene.camera, 0, slide), screen.width() / screen.height()) * glm::vec4(at, 1.0f);
                    return clip.w > 0 ? (1 - clip.y / clip.w) / 2 * screen.height() - want : 0.0f;
                };
                float before = 0;
                float slide = 1;
                for (int i = 0; i < 4 && screen.height() > 0; ++i)
                {
                    const float a = off(before);
                    const float b = off(slide);
                    if (std::abs(b - a) < 1e-3f)
                        break;
                    before = std::exchange(slide, slide - b * (slide - before) / (b - a));
                }
                scene.camera = rct3::park::on_ground(rct3::park::panned(scene.camera, 0, slide), scene.grid);
            }
        }
        if (auto tools = desktop.root().find_as<ui::screens::park_view::ToolRow>(kToolsName))
        {
            // A menu opening puts the tool out away, and an item list goes with the menu it came from. The view
            // menu's switches are for use while building, so it leaves the tool out.
            if (const auto menu = tools->open_menu(); menu != scene.menu_shown)
            {
                close_build_list(desktop.root());
                if (menu && *menu != ui::screens::park_view::ParkTool::View)
                {
                    put_tools_away(scene);
                    hide_info(scene);
                }
                scene.menu_shown = menu;
            }
            const std::optional<ui::screens::park_view::ParkTool> pressed = tools->take_pressed();
            if (pressed == ui::screens::park_view::ParkTool::Leave)
                ask_leave(scene, *assets.strings, assets.palette);
            else if (pressed == ui::screens::park_view::ParkTool::Map)
            {
                put_tools_away(scene);
                hide_info(scene);
                toggle_park_map(scene.map, desktop, *assets.strings, assets.palette);
            }
            else if (pressed == ui::screens::park_view::ParkTool::Paths)
            {
                tools->close_menu();
                track::close_track_building(scene); // One tool panel at the bottom left at a time
                stop_ride_placing(scene);
                stop_land_tools(scene);
                stop_deleting(scene);
                stop_structure_tools(scene);
                stop_pooling(scene);
                toggle_footpath_panel(device, scene, assets, options);
            }
            if (const auto item = tools->take_item())
            {
                if (*item == ui::screens::park_view::ToolItem::ShapeGround || *item == ui::screens::park_view::ToolItem::PaintGround || *item == ui::screens::park_view::ToolItem::Water || *item == ui::screens::park_view::ToolItem::Waterfalls)
                {
                    track::close_track_building(scene);
                    stop_ride_placing(scene);
                    close_footpath_panel(scene);
                    stop_deleting(scene);
                    stop_structure_tools(scene);
                    stop_pooling(scene);
                    using enum LandTools::Tool;
                    toggle_land_tool(scene, *item == ui::screens::park_view::ToolItem::ShapeGround ? Shape : (*item == ui::screens::park_view::ToolItem::PaintGround ? Paint : (*item == ui::screens::park_view::ToolItem::Water ? Water : Waterfall)), assets);
                }
                else if (*item == ui::screens::park_view::ToolItem::DeleteOne || *item == ui::screens::park_view::ToolItem::DeleteArea)
                {
                    track::close_track_building(scene);
                    stop_ride_placing(scene);
                    close_footpath_panel(scene);
                    stop_land_tools(scene);
                    stop_structure_tools(scene);
                    stop_pooling(scene);
                    const auto tool = *item == ui::screens::park_view::ToolItem::DeleteOne ? DeleteTools::Tool::One : DeleteTools::Tool::Area;
                    const bool out = scene.deleting.tool == tool;
                    stop_deleting(scene);
                    if (!out)
                        scene.deleting.tool = tool;
                    if (!out && tool == DeleteTools::Tool::Area)
                        desktop.root().add<ui::screens::build::DeleteAreaPanel>(std::string(kDeleteAreaName), skins::delete_area_panel_style(), *assets.strings);
                }
                else if (*item == ui::screens::park_view::ToolItem::StructureGrab || *item == ui::screens::park_view::ToolItem::StructureList)
                {
                    track::close_track_building(scene);
                    stop_ride_placing(scene);
                    close_footpath_panel(scene);
                    stop_land_tools(scene);
                    stop_deleting(scene);
                    stop_pooling(scene);
                    toggle_structure_tool(scene, *item == ui::screens::park_view::ToolItem::StructureGrab ? StructureTools::Tool::Grab : StructureTools::Tool::Place, assets);
                }
                else if (*item == ui::screens::park_view::ToolItem::Pools)
                {
                    // The pools button opens the pool panel, as in the game, not a list.
                    track::close_track_building(scene);
                    stop_ride_placing(scene);
                    close_footpath_panel(scene);
                    stop_land_tools(scene);
                    stop_deleting(scene);
                    stop_structure_tools(scene);
                    stop_enclosing(scene);
                    close_build_list(desktop.root());
                    toggle_pool_panel(device, scene, assets, ui::currency(*assets.strings, options.choice(config::game::GameOption::Currency)));
                }
                else if (*item == ui::screens::park_view::ToolItem::People)
                    scene.show_people = tools->switched_on(*item);
                else if (*item == ui::screens::park_view::ToolItem::Scenery)
                    scene.show_scenery = tools->switched_on(*item);
                else if (*item == ui::screens::park_view::ToolItem::SeeThroughGround)
                    scene.see_through_ground = !tools->switched_on(*item); // Switched on, the ground shows whole
                else if (*item == ui::screens::park_view::ToolItem::Grid)
                    scene.show_grid = tools->switched_on(*item);
                else if (*item == ui::screens::park_view::ToolItem::Contours)
                    scene.show_contours = tools->switched_on(*item);
                else if (*item == ui::screens::park_view::ToolItem::PathHeights)
                    scene.show_path_heights = tools->switched_on(*item);
                else if (*item == ui::screens::park_view::ToolItem::RideHeights)
                    scene.show_ride_heights = tools->switched_on(*item);
                else if (*item == ui::screens::park_view::ToolItem::Money)
                    scene.show_money = tools->switched_on(*item);
                else if (*item == ui::screens::park_view::ToolItem::GuestPayments)
                    scene.show_guest_payments = tools->switched_on(*item);
                else if (*item == ui::screens::park_view::ToolItem::Supports)
                {
                    scene.show_supports = tools->switched_on(*item);
                    scene.levels = { };
                    scene.since_rebuild = kObjectRebuildInterval;
                }
                else if (*item == ui::screens::park_view::ToolItem::Options || *item == ui::screens::park_view::ToolItem::About || *item == ui::screens::park_view::ToolItem::Save || *item == ui::screens::park_view::ToolItem::Load)
                {
                    put_tools_away(scene);
                    hide_info(scene);
                    scene.window_requests.push_back(*item);
                }
                else if (const auto topic = ui::screens::management::management_topic(*item))
                    toggle_management_window(desktop, *topic, management_facts(scene), *assets.strings, assets.palette, info_units(assets, options));
                else
                    open_build_list(scene.build, desktop.root(), *item, tools->item(*item), *tools, *assets.strings, assets.palette, ui::currency(*assets.strings, options.choice(config::game::GameOption::Currency)));
            }
            // A ride's window names it after its kind, looked up by the kind's symbol; some kinds' files give their
            // name under another key, so once the items are read each such kind's name is kept under its symbol too.
            if (!scene.build.kinds_named && scene.build.items.valid() && scene.build.items.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                scene.build.kinds_named = true;
                for (const rct3::park::BuildItem& item : scene.build.items.get())
                {
                    const std::string own = std::format("{}Name", item.symbol);
                    if (const auto name = assets.strings->get(item.name_text); !name.empty() && assets.strings->get_ignoring_case(own).empty())
                        assets.strings->add(own, std::u16string(name));
                }
                // A ride's window opened before then named it from the kind's name it could not yet find.
                if (info && info->visible)
                    show_info_page(scene, *info, assets, options);
            }
            // The saved designs are read once the folders are known: the player's own, in our folder and in the
            // game's, then the install's.
            // shortcut: read once a park; a design saved meanwhile shows in the next park.
            if (!scene.build.designs.valid() && assets.install && scene.saves.data)
            {
                std::vector<std::filesystem::path> folders { scene.saves.data->folder(config::UserFolder::Coasters) };
                if (const auto name = config::game::game_saves_folder(config::UserFolder::Coasters); scene.saves.game_saves && name)
                    folders.push_back(*scene.saves.game_saves / *name);
                folders.push_back(assets.install->find("Coaster Designs").value_or(std::filesystem::path { }));
                start_design_scan(scene.build, std::move(folders));
            }
            if (const auto picked = update_build_menus(scene.build, desktop.root(), scene.park.cash))
            {
                bool building = false;
                if (start_pooling(scene, *picked))
                {
                    stop_enclosing(scene);
                    stop_ride_placing(scene);
                    track::close_track_building(scene);
                    close_footpath_panel(scene);
                    stop_land_tools(scene);
                    stop_deleting(scene);
                    stop_structure_tools(scene);
                }
                else if (start_enclosing(scene, *picked))
                {
                    stop_pooling(scene);
                    stop_ride_placing(scene);
                    track::close_track_building(scene);
                    close_footpath_panel(scene);
                    stop_land_tools(scene);
                    stop_deleting(scene);
                    stop_structure_tools(scene);
                }
                else if (start_ride_placing(scene, *picked))
                {
                    stop_enclosing(scene);
                    stop_pooling(scene);
                    track::close_track_building(scene);
                    close_footpath_panel(scene);
                    stop_land_tools(scene);
                    stop_deleting(scene);
                    stop_structure_tools(scene);
                    building = picked->kind != rct3::park::BuildKind::Scenery;
                }
                else if (track::new_tracked_ride(scene, *picked, assets))
                {
                    stop_enclosing(scene);
                    stop_pooling(scene);
                    stop_ride_placing(scene);
                    close_footpath_panel(scene);
                    stop_land_tools(scene);
                    stop_deleting(scene);
                    stop_structure_tools(scene);
                    building = true;
                }
                // Building a ride puts the build menus away, as in the game; scenery keeps its list out for the
                // next piece.
                if (building)
                {
                    close_build_list(desktop.root());
                    if (auto tools = desktop.root().find_as<ui::screens::park_view::ToolRow>(kToolsName); tools && tools->open_menu() && (*tools->open_menu() == ui::screens::park_view::ParkTool::Rides || *tools->open_menu() == ui::screens::park_view::ParkTool::Shops || *tools->open_menu() == ui::screens::park_view::ParkTool::Scenery || *tools->open_menu() == ui::screens::park_view::ParkTool::Paths))
                        tools->close_menu();
                }
            }
        }
        // The window of a ride, a stall, a person or a piece of scenery shares its place with the tools' panels: the
        // one that opens puts the other away. The doors of a ride just placed go on being placed beside its window.
        if (info)
        {
            if (info->visible && tool_panel_out(scene))
            {
                if (scene.info_shown)
                    hide_info(scene);
                else
                    put_tools_away(scene, true);
            }
            scene.info_shown = info->visible;
        }
        update_view_lines(device, scene);
        if (auto heights = desktop.root().find_as<ui::widgets::WorldLabels>(kHeightsName))
        {
            const ui::Rect view = desktop.root().rect();
            heights->labels = height_labels(scene, { view.width(), view.height() });
            // While the park bar's highlights are on, the people the player picked have their name and what
            // they are doing floating over them.
            if (const auto bar = desktop.root().find_as<ui::screens::park_view::ParkBar>(kBarName); bar && bar->highlights() && view.width() > 0 && view.height() > 0)
            {
                constexpr float kHighlightLift = 2.4f; ///< Chosen by eye: metres over their feet, clear of their head
                const glm::mat4 camera = rct3::park::view_projection(scene.camera, view.width() / view.height());
                for (const ShownPerson& p : scene.shown_people)
                {
                    const ui::screens::info::PersonFacts f { .guest = p.staff ? nullptr : &scene.people.guests[p.saved], .staff = p.staff ? &scene.people.staff[p.saved] : nullptr, .people = &scene.people, .rides = &scene.rides };
                    if (!f.person()->highlight)
                        continue;
                    const glm::vec4 clip = camera * (p.pose.world * glm::vec4(0, kHighlightLift, 0, 1));
                    const glm::vec2 ndc = glm::vec2(clip) / clip.w;
                    if (clip.w > 0 && std::abs(ndc.x) <= 1 && std::abs(ndc.y) <= 1)
                        heights->labels.push_back({ { (ndc.x + 1) / 2 * view.width(), (1 - ndc.y) / 2 * view.height() }, ui::screens::info::person_name(f, *assets.strings) + u"\n" + ui::screens::info::person_doing(f, *assets.strings) });
                }
            }
        }
        if (auto money = desktop.root().find_as<ui::widgets::MoneyFloaters>(kMoneyName))
        {
            const ui::Rect view = desktop.root().rect();
            money->floats = money_floats(scene, { view.width(), view.height() }, ui::currency(*assets.strings, options.choice(config::game::GameOption::Currency)), elapsed);
        }
        if (const auto picked = update_park_map(device, scene.map, desktop, scene.grid, scene.camera, aspect(device), [&] { return map_people(scene); }, elapsed))
        {
            scene.camera.target = { picked->x, scene.camera.target.y, picked->y };
            scene.camera = rct3::park::on_ground(scene.camera, scene.grid);
        }
        // What the staff window shows of each worker that the save does not hold: their clothes and patrol.
        scene.staff_looks.assign(scene.people.staff.size(), { });
        for (const ShownPerson& p : scene.shown_people)
        {
            if (!p.staff || p.saved >= scene.staff_looks.size())
                continue;
            ui::screens::management::StaffLook& look = scene.staff_looks[p.saved];
            look.colours = { p.pose.look.colours.at(ui::screens::info::kUpperPalette), p.pose.look.colours.at(ui::screens::info::kUpperPalette + 1) };
            if (const auto* patrol = scene.sim.registry.valid(p.guest) ? scene.sim.registry.try_get<simulation::staff::Patrol>(p.guest) : nullptr)
                look.patrol_tiles = patrol->tiles.size();
            look.painting_patrol = p.guest != entt::null && p.guest == shown_staff(scene) && scene.painting_patrol;
        }
        const ui::screens::management::ManagementFacts facts = management_facts(scene);
        const ManagementAsks asks = update_management_windows(desktop, facts, *assets.strings, info_units(assets, options));
        if (asks.row && info)
            open_row(scene, *info, *asks.row, assets, options);
        if (asks.export_ledger && scene.saves.data)
        {
            // A new file each time, beside those written before.
            auto folder = scene.saves.data->make_folder(config::UserFolder::Exports);
            std::filesystem::path path;
            for (int n = 1; folder && (path.empty() || std::filesystem::exists(path)); ++n)
                path = *folder / std::format("ledger_{}.csv", n);
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << ui::screens::management::ledger_csv(facts, *assets.strings);
            if (folder && out)
                simulation::park::post(scene.park.messages, { .text_key = "Rct3pp_Finance_Exported", .subject_name = path.string() });
            else
                logging::logger("ui").warning("ledger: writing {} failed", folder ? path.string() : folder.error());
        }
        // A worker hired is handed to the pointer at once, to be put down where the player likes, or given back
        // to go to the park's entrance; whoever was held before goes back first.
        if (const auto hired = asks.staff.hire ? hire(scene, *asks.staff.hire) : std::nullopt)
        {
            (void)hold_person(scene, { }, { }, false, true);
            if (pick_up_person(scene, *hired))
                scene.holding->from = simulation::entrance_drop(scene.sim).value_or(scene.holding->from);
        }
        // The staff window names workers by their place in the park's staff; the scene by the person shown.
        const auto shown_worker = [&](std::size_t staff) -> std::optional<std::size_t> {
            const auto p = std::ranges::find_if(scene.shown_people, [&](const ShownPerson& p) { return p.staff && p.saved == staff; });
            return p != scene.shown_people.end() ? std::optional(static_cast<std::size_t>(p - scene.shown_people.begin())) : std::nullopt;
        };
        // A worker's patrol button in the list opens their details, where painting their area is done.
        if (asks.staff.change && asks.staff.change->second.id == ui::screens::info::person_control(ui::screens::info::PersonControl::PatrolPaint))
            scene.person_tab = ui::screens::info::PersonTab::General;
        if (const auto who = asks.staff.open ? shown_worker(*asks.staff.open) : std::nullopt; who && info)
            open_person(scene, *info, *who, assets, options);
        if (const auto who = asks.staff.centre ? shown_worker(*asks.staff.centre) : std::nullopt)
        {
            const glm::vec3 at = scene.shown_people[*who].pose.world[3];
            scene.camera.target = { at.x, scene.camera.target.y, at.z };
            scene.camera = rct3::park::on_ground(scene.camera, scene.grid);
        }
        if (const auto paint = asks.staff.paint; paint && shown_worker(paint->staff))
            paint_person(scene, *shown_worker(paint->staff), paint->pot, paint->colour);
        if (const auto change = asks.staff.change; change && shown_worker(change->first))
        {
            const std::size_t who = *shown_worker(change->first);
            if (change->second.id == ui::screens::info::person_control(ui::screens::info::PersonControl::Sack))
            {
                if (info && scene.shown_person == who)
                {
                    info->hide();
                    scene.shown_person.reset();
                }
                fire(scene, who);
            }
            else
            {
                // The info panel's changes go to the worker it shows; this one is shown for the change alone,
                // unless the patrol button opened their panel for painting.
                const auto kept = std::exchange(scene.shown_person, who);
                const bool changed = change_person(scene, who, change->second);
                if (change->second.id != ui::screens::info::person_control(ui::screens::info::PersonControl::PatrolPaint))
                    scene.shown_person = kept;
                if (changed && info && scene.shown_person == who)
                    show_info_page(scene, *info, assets, options);
            }
        }
        // Each listed ride that may take the state takes it; the others stay as they are.
        for (const ui::screens::management::ManagementRow& r : asks.listed)
        {
            if (r.kind == ui::screens::management::ManagementRow::Kind::Ride && r.index < scene.rides.rides.size())
                (void)change_ride_state(scene, r.index, *asks.state_all);
            else if (r.kind == ui::screens::management::ManagementRow::Kind::Stall && r.index < scene.rides.stalls.size())
                set_attraction_state(scene, scene.rides.stalls[r.index].base, *asks.state_all);
        }
        if (asks.park_open)
        {
            auto& flags = scene.park_model.park.flags.raw;
            flags = *asks.park_open ? flags | std::to_underlying(save::model::ParkFlag::Open) : flags & ~std::to_underlying(save::model::ParkFlag::Open);
            for (auto [e, gate] : scene.sim.registry.view<simulation::ParkGate>().each())
                gate.closed = !*asks.park_open;
        }
        // Each press goes to the next entrance, round to the first.
        if (asks.centre_entrance)
        {
            std::vector<std::size_t> entrances;
            for (std::size_t i = 0; i < scene.objects.size(); ++i)
                if (park_entrance(scene, scene.objects[i]))
                    entrances.push_back(i);
            if (!entrances.empty())
            {
                const save::model::PlacedObject& o = scene.objects[entrances[scene.entrance_centred++ % entrances.size()]];
                const glm::vec2 middle = rct3::park::object::footprint_middle(o, scene.models.at(rct3::park::object::model_ref(o)).squares);
                scene.camera.target = { scene.grid.origin_x + middle.x * scene.grid.tile_size_x, scene.camera.target.y, scene.grid.origin_z + middle.y * scene.grid.tile_size_z };
                scene.camera = rct3::park::on_ground(scene.camera, scene.grid);
            }
        }
        if (asks.buy_land || asks.buy_rights)
            buy_land_tool(scene, asks.buy_rights ? rct3::park::LandBuy::Rights : rct3::park::LandBuy::Land, assets);
        if (asks.borrow)
            simulation::park::change_loan(scene.park, { scene.park_model.finance.max_loan }, *asks.borrow);
        // A step at a time, as the buttons do, until the loan reaches the bar's or can move no more.
        if (asks.loan)
        {
            const bool borrow = *asks.loan > scene.park.loan;
            while (*asks.loan != scene.park.loan && (*asks.loan > scene.park.loan) == borrow && simulation::park::change_loan(scene.park, { scene.park_model.finance.max_loan }, borrow))
            {
            }
        }
        if (asks.research_budget)
            scene.research.budget = *asks.research_budget;
        if (asks.research_focus && scene.research_items)
            (void)simulation::focus_research(scene.research, *asks.research_focus, *scene.research_items);
        // Areas past the end of the priorities weigh the most until set.
        if (const auto& p = asks.research_priority; p && p->first < simulation::kResearchAreas)
        {
            if (scene.research.priorities.size() <= p->first)
                scene.research.priorities.resize(p->first + 1, 1.0f);
            scene.research.priorities[p->first] = p->second;
        }
        // Every broken path item back new at once, at the full price of each: the dear way, a handyman's the free one.
        if (asks.replace_broken && facts.broken_items > 0 && simulation::park::book(scene.park, scene.park_model.finance, scene.ledger_month, facts.replace_cost, simulation::park::LedgerLine::Landscaping, false))
            simulation::replace_broken(scene.sim);
        profiling::section("Footpaths", [&] { update_footpaths(device, scene, assets, options); });
        profiling::section("Supports", [&] { refresh_supports(scene, assets); });
        follow_footpaths(scene, elapsed);
        if (auto guide = desktop.root().find_as<ui::screens::tutorial::TutorialGuide>(kGuideName); guide && guide->take_quit())
            ask_leave(scene, *assets.strings, assets.palette);
        if (auto question = desktop.root().find_as<ui::widgets::QuestionBox>(kQuestionName))
        {
            if (const auto answer = question->take_answer())
                scene.leave = scene.leave || *answer == 0;
        }
        if (auto question = desktop.root().find_as<ui::widgets::QuestionBox>(kSellPoolName))
        {
            if (const auto answer = question->take_answer(); answer && *answer == 0)
                if (const auto room = std::exchange(scene.pool_complex_asked, std::nullopt); room && *room < scene.pools.rooms.size())
                {
                    delete_pool_complex(device, scene, assets, *room);
                    if (auto info = desktop.root().find_as<ui::screens::info::InfoPanel>(kInfoName); info && scene.shown_prop)
                    {
                        scene.shown_prop.reset();
                        info->hide();
                    }
                }
        }
        if (auto question = desktop.root().find_as<ui::widgets::QuestionBox>(kLakeName))
        {
            if (const auto answer = question->take_answer(); answer && *answer == 0)
                if (const auto tile = std::exchange(scene.land.lake_asked, std::nullopt))
                    remove_lake(device, scene, assets, *tile);
        }
        if (auto question = desktop.root().find_as<ui::widgets::QuestionBox>(kFloodName))
        {
            if (const auto answer = question->take_answer(); answer && *answer == 0)
                remove_falls(device, scene, assets, std::exchange(scene.land.falls_asked, { }));
        }
        if (auto question = desktop.root().find_as<ui::widgets::QuestionBox>(kDemolishName))
        {
            if (const auto answer = question->take_answer(); answer && *answer == 0)
                for (const rct3::park::AttractionRef ref : std::exchange(scene.demolish_asked, { }))
                    demolish(scene, assets, ref);
        }
        // Prizes won are told in a box each, as the player closes the one before.
        if (auto box = desktop.root().find_as<ui::widgets::QuestionBox>(kPrizeName))
            (void)box->take_answer();
        else if (!scene.prizes.empty())
        {
            const ui::widgets::QuestionBoxStyle style = skins::question_box_style(assets.palette);
            desktop.root().add<ui::widgets::QuestionBox>(std::string(kPrizeName), style, std::u16string(assets.strings->get(scene.prizes.front())), std::vector<ui::widgets::QuestionAnswer> { { std::u16string(assets.strings->get(kOk)), style.confirm_icon } });
            scene.prizes.erase(scene.prizes.begin());
        }
    }

    bool take_park_leave(ParkScene& scene)
    {
        return std::exchange(scene.leave, false);
    }

    bool take_park_alert(ParkScene& scene)
    {
        return std::exchange(scene.message_alert, false);
    }

    rct3::park::object::LevelScales level_scales(const config::game::GameOptions& options)
    {
        static_assert(config::game::kDetailPairs.size() == rct3::park::kDetailKinds); // In the same order
        rct3::park::object::LevelScales scales;
        for (std::size_t i = 0; i < scales.size(); ++i)
            scales[i] = { .detail = options.get(config::game::kDetailPairs[i].detail), .distance = options.get(config::game::kDetailPairs[i].distance) };
        return scales;
    }

    render::Color park_background(const ParkScene& scene)
    {
        return scene.horizon;
    }

    ui::CursorKind park_scene_cursor(const ParkScene& scene)
    {
        ui::ParkPointer pointer;
        pointer.interface = scene.desktop->cursor();
        pointer.turning = scene.turning;
        pointer.panning = scene.dragging;
        pointer.zooming = scene.zooming;
        pointer.over_pickable = scene.hover_pickable;
        // While the path panel shows its controls, a click on the ground starts a path there.
        if (auto paths = scene.desktop->root().find_as<const ui::screens::build::FootpathPanel>(kFootpathPanelName); paths && paths->showing() && paths->page() == ui::screens::build::FootpathPage::Controls)
            pointer.tool = ui::PointerTool::Path;
        return ui::park_cursor(pointer);
    }

    std::vector<std::byte> save_park(const ParkScene& scene)
    {
        parkfile::SavedPark saved;
        saved.base = scene.base;
        saved.rides = scene.rides;
        // What was pulled down only keeps its place while the park is shown.
        std::erase_if(saved.rides.rides, [](const save::model::Ride& r) { return r.base.demolished; });
        std::erase_if(saved.rides.stalls, [](const save::model::Stall& s) { return s.base.demolished; });
        // What each attraction has taken goes into its totals, which the simulation counts afresh from when loaded.
        for (const auto [e, a] : scene.sim.registry.view<simulation::Attraction>().each())
        {
            const auto fold = [&](save::model::AttractionCommon& c) {
                if (a.saved != 0 && c.id == a.saved)
                {
                    c.total_profit += a.income;
                    c.total_guests += a.customers;
                }
            };
            std::ranges::for_each(saved.rides.rides, [&](save::model::Ride& r) { fold(r.base); });
            std::ranges::for_each(saved.rides.stalls, [&](save::model::Stall& s) { fold(s.base); });
        }
        saved.objects = parkfile::kept_objects(scene.objects, scene.own_objects, scene.placed, scene.footpaths.paths, saved.rides);
        if (auto summary = save::read_summary(scene.base))
            saved.summary = std::move(*summary);
        // shortcut: the picture stays the original park's and the day is the week's first; draw the park and
        // count days when the calendar does.
        saved.summary.year = static_cast<std::int32_t>(scene.park.date.year) + 1;
        saved.summary.month = scene.park.date.month;
        saved.summary.day = scene.park.date.week * 7 + 1;
        saved.summary.minutes = static_cast<std::int32_t>(scene.day.time * 24 * 60);
        saved.summary.guests = scene.park.guests;
        // The pool tool's work shown is not built: the park is saved as it was before it.
        saved.grid = scene.grid;
        for (const auto& [tile, corners] : scene.pooling.dug)
            saved.grid.at(tile.col, tile.row).corners = corners;
        saved.paths = scene.footpaths.paths;
        saved.bins = scene.bins;
        saved.signs = scene.signs;
        const simulation::park::ParkState& park = scene.park;
        saved.progress = { .cash = park.cash,
                           .loan = park.loan,
                           .rating = park.rating,
                           .rating_parts = park.rating_parts,
                           .time = park.time,
                           .zero_time = park.zero_time,
                           .date = park.date,
                           .weather = park.weather,
                           .forecast = park.forecast,
                           .temperature = park.temperature,
                           .day_time = scene.day.time,
                           .climate = park.climate,
                           .weather_hold = park.weather_hold,
                           .wind = park.wind,
                           .wind_change = park.wind_change,
                           .ledger = scene.park_model.finance.ledger,
                           .max_loan = scene.park_model.finance.max_loan,
                           .interest_rate_percent = scene.park_model.finance.interest_rate_percent,
                           .ledger_month = scene.ledger_month };
        {
            parkfile::SavedProgress& p = saved.progress;
            const save::model::Finance& m = scene.park_model.finance;
            p.cash_less_loan = m.cash_less_loan;
            p.company_value = m.company_value;
            p.expenditure = m.expenditure;
            p.income = m.income;
            p.park_value = m.park_value;
            p.share_price = m.share_price;
            p.weekly_profit = m.weekly_profit;
            const save::model::Park& g = scene.park_model.park;
            p.park_rating = g.park_rating;
            p.guests_in_park = g.guests_in_park;
            p.adults_in_park = g.adults_in_park;
            p.teenagers_in_park = g.teenagers_in_park;
            p.children_in_park = g.children_in_park;
            p.guest_intake = g.guest_intake;
            p.publicity_level = g.publicity_level;
            const auto [intake, income] = week_totals(scene);
            p.week_intake = intake - scene.week_began.intake;
            p.week_income = income - scene.week_began.income;
            p.week_cash = scene.week_began.cash;
        }
        saved.camera = scene.camera;
        std::unordered_map<entt::entity, std::int32_t> looks;
        for (const ShownPerson& p : scene.shown_people)
        {
            if (p.guest != entt::null)
                looks.emplace(p.guest, static_cast<std::int32_t>(p.saved));
        }
        saved.people = parkfile::keep_people(scene.sim, scene.people, looks);
        saved.no_queue_signs.assign(scene.led_signs.hidden.begin(), scene.led_signs.hidden.end());
        saved.shows = scene.shows;
        if (saved.shows)
            std::erase_if(*saved.shows, [&](const save::model::RideShow& show) { return !std::ranges::contains(saved.rides.rides, show.ride, [](const save::model::Ride& r) { return r.base.id; }); });
        saved.waterfalls = scene.footpaths.waterfalls;
        saved.day_mode = scene.day.mode;
        saved.animals = parkfile::keep_animals(scene.sim, scene.enclosures);
        saved.pools = scene.pooling.before ? *scene.pooling.before : scene.pools; // Even none, so the game's pools stay sold
        for (const auto [e, item] : scene.sim.registry.view<const simulation::PathItem>().each())
            if (item.broken && item.object != 0)
                saved.broken_items.push_back(item.object);
        for (const auto [e, bed] : scene.sim.registry.view<const simulation::FlowerBed>().each())
            saved.flower_beds.push_back({ .object_ref = bed.object, .water = bed.water });
        if (!scene.park_model.objectives.objectives.empty())
            saved.goals = parkfile::SavedGoals { .objectives = scene.park_model.objectives };
        saved.research = scene.research;
        return parkfile::write_saved_park(saved);
    }

} // namespace rct3::scene::park
