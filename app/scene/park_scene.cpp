// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park_scene.hpp"

#include "core/maybe.hpp"
#include "park/daylight.hpp"
#include "park/edge_walls.hpp"
#include "park/item_category.hpp"
#include "park/object_models.hpp"
#include "park/object_pick.hpp"
#include "park/object_placement.hpp"
#include "park/ride_fences.hpp"
#include "park/sky_look.hpp"
#include "parkfile/saved_park.hpp"
#include "profiling/profiler.hpp"
#include "save/model/park.hpp"
#include "save/model/people.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"
#include "scene/park_footpaths.hpp"
#include "scene/park_management.hpp"
#include "scene/park_map_view.hpp"
#include "scene/park_patrol.hpp"
#include "scene/park_refresh.hpp"
#include "scene/park_setup.hpp"
#include "scene/park_view.hpp"
#include "scene/park_water.hpp"
#include "scene/track_building.hpp"
#include "simulation/awards.hpp"
#include "simulation/components.hpp"
#include "simulation/import_park.hpp"
#include "simulation/staff_care.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"
#include "strings/ascii.hpp"
#include "strings/unicode.hpp"
#include "ui/game_style.hpp"
#include "ui/language.hpp"
#include "ui/screens/attraction_info.hpp"
#include "ui/screens/message_drawer.hpp"
#include "ui/screens/ride_camera_panel.hpp"
#include "ui/screens/tool_menus.hpp"
#include "ui/screens/tool_row.hpp"
#include "ui/screens/tutorial_guide.hpp"
#include "ui/widgets/edit_box.hpp"
#include "ui/widgets/question_box.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <format>
#include <future>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace rct3::scene
{

    namespace
    {

        namespace skins = ui::game_skins;

        constexpr float kPropViewClosest = 1.5f;    ///< Small props fill the round view
        constexpr float kViewPitch = 0.5f;          ///< Radians down, of the info panel's round view
        constexpr float kViewTurn = 0.15f;          ///< Radians a second the round view turns round its attraction
        constexpr float kFocusZoomOut = 2;          ///< A message's camera looks from this many times the round view's distance (ours)
        constexpr float kFocusClosest = 20;         ///< But no closer, so a small stall shows with what is round it (ours)
        constexpr float kPersonViewDistance = 4.5f; ///< Of the round view's eye from the person it shows
        constexpr float kPersonViewHeight = 0.9f;   ///< Of the point it looks at, above the person's feet
        // Values for photos at photo spots.
        constexpr float kPhotoPitch = 0.15f;     ///< Radians down: near eye level
        constexpr float kPhotoClosest = 3;       ///< Of the eye from the party
        constexpr float kPhotoPersonTall = 1.8f; ///< Framed from the feet up to this high
        /// Of the pointer between pressing and letting go, in interface units, beyond which it is no click.
        constexpr float kClickSlack = 4;
        constexpr int kFahrenheit = 1; ///< The temperature units option's choice for Fahrenheit
        constexpr std::string_view kQuestionName = "leave_tutorial";
        constexpr std::string_view kLeaveText = "TutorialWindow_ConfirmQuit";
        constexpr std::string_view kTapeFence = "KeepClearFence"; ///< Closes a broken ride off until it is fixed
        constexpr std::string_view kTapeFenceOverlay = "Style\\Vanilla\\style";
        constexpr std::string_view kYes = "SMB_Label_Yes";
        constexpr std::string_view kNo = "SMB_Label_No";

        // How far the controls move the camera.
        constexpr float kDragPan = 0.0025f;    ///< Per pixel, times the distance
        constexpr float kDragTurn = 0.006f;    ///< Radians per pixel
        constexpr float kKeyPan = 0.8f;        ///< Per second, times the distance
        constexpr float kEdgeBand = 12;        ///< Interface units in from a screen edge where the pointer pushes
        constexpr float kWheelZoom = 0.85f;    ///< Distance times this per notch towards the screen
        constexpr float kWheelSeconds = 0.25f; ///< How long a notch takes to roll in, nearly all of it
        constexpr float kKeyZoom = 2.5f;       ///< Times per second
        constexpr float kKeyTurn = 1.5f;       ///< Radians per second

        constexpr float kHoverPickInterval = 0.1f; ///< Seconds between looks at what is under the pointer, for its look

        void ask_leave(ParkScene& scene, const ui::StringTable& strings, const ui::Palette& palette)
        {
            if (scene.desktop->root().find(kQuestionName))
                return;
            const ui::QuestionBoxStyle style = skins::question_box_style(palette);
            scene.desktop->root().add<ui::QuestionBox>(std::string(kQuestionName), style, std::u16string(strings.get(kLeaveText)), std::vector<ui::QuestionAnswer> { { std::u16string(strings.get(kYes)), style.confirm_icon }, { std::u16string(strings.get(kNo)), style.cancel_icon } });
        }

        void move_camera(ParkScene& scene, const ParkInput& input, bool over_interface, bool edge_scroll, float elapsed)
        {
            const ui::Point p = input.pointer.position;
            const float dx = p.x - scene.last_pointer.x;
            const float dy = p.y - scene.last_pointer.y;
            scene.last_pointer = p;

            // A drag starts away from the windows and goes on wherever the pointer then goes.
            const bool turn = input.middle || (input.right && input.pointer.down);
            const bool drag = input.right && !turn;
            scene.turning = turn && (scene.turning || !over_interface);
            scene.dragging = drag && (scene.dragging || !over_interface);

            park::ParkCamera c = scene.camera;
            const bool free = scene.free_camera;
            if (scene.turning)
                c = park::turned(c, dx * kDragTurn, dy * kDragTurn, free);
            else if (scene.dragging)
                c = park::panned(c, -dx * kDragPan * c.distance, dy * kDragPan * c.distance);
            if (!over_interface)
                scene.zoom_pending += input.pointer.wheel.y;
            if (scene.zoom_pending != 0)
            {
                const float notches = park::zoom_step(scene.zoom_pending, elapsed, kWheelSeconds);
                scene.zoom_pending -= notches;
                const park::ParkCamera before = c;
                c = park::zoomed(c, std::pow(kWheelZoom, notches), free);
                if (c.distance == before.distance)
                    scene.zoom_pending = 0; // At a limit: what is left would roll in late once turned back
            }

            const float step = kKeyPan * c.distance * elapsed;
            float right = (input.right_key ? step : 0) - (input.left_key ? step : 0);
            float forward = (input.up_key ? step : 0) - (input.down_key ? step : 0);
            if (edge_scroll && input.in_window && !over_interface && !scene.turning && !scene.dragging)
            {
                const float scale = scene.scale.value_or(1);
                const ui::Rect screen = scene.desktop->root().rect();
                const glm::vec2 push = park::edge_push({ p.x, p.y }, glm::vec2 { screen.width(), screen.height() } * scale, kEdgeBand * scale);
                right += push.x * step;
                forward += push.y * step;
            }
            if (right != 0 || forward != 0)
                c = park::panned(c, right, forward);
            if (input.zoom_in_key != input.zoom_out_key)
                c = park::zoomed(c, std::pow(kKeyZoom, (input.zoom_in_key ? -1.0f : 1.0f) * elapsed), free);
            const float yaw = (input.turn_right_key ? 1.0f : 0.0f) - (input.turn_left_key ? 1.0f : 0.0f);
            const float pitch = (input.tilt_down_key ? 1.0f : 0.0f) - (input.tilt_up_key ? 1.0f : 0.0f);
            if (yaw != 0 || pitch != 0)
                c = park::turned(c, yaw * kKeyTurn * elapsed, pitch * kKeyTurn * elapsed, free);
            scene.camera = free ? c : park::on_ground(c, scene.grid);
        }

        /// The sky and the light as the day clock shows them, with a look that is fading out over them.
        void show_time_of_day(ParkScene& scene, glm::vec3 viewer)
        {
            const simulation::DayClock& day = scene.day;
            const park::Daylight light = park::mixed(park::daylight(day.shown), park::daylight(day.faded), day.fade);
            scene.light = { .sun_direction = light.sun_direction, .sun = light.sun, .sky = light.sky, .ground = light.ground };
            const float column = park::sky_column(day.shown);
            const float faded = park::sky_column(day.faded);
            const float night = glm::mix(park::sky_darkness(day.shown), park::sky_darkness(day.faded), day.fade);
            scene.darkness = night;
            scene.sky_mesh = park::sky_mesh(scene.sky, { .eye = viewer, .radius = kSkyRadius, .day = column, .faded_day = faded, .fade = day.fade, .night = night, .time = scene.clock });
            if (!scene.horizon_band.empty())
            {
                const glm::vec4 c = glm::mix(park::band_colour(scene.horizon_band, column), park::band_colour(scene.horizon_band, faded), day.fade);
                scene.horizon = { c.r, c.g, c.b, c.a };
            }
            // Far land fades into the horizon's colour.
            scene.light.haze = { scene.horizon.r, scene.horizon.g, scene.horizon.b };
            scene.light.haze_rate = scene.haze_rate;
            scene.light.surface_detail = scene.surface_detail;
        }

        /// The first of the attraction's objects that takes colours, and the slots it takes.
        std::optional<std::pair<std::size_t, std::vector<std::size_t>>> painted_object(const ParkScene& scene, park::AttractionRef ref)
        {
            for (std::size_t i = 0; i < scene.objects.size(); ++i)
            {
                const auto owner = scene.attractions.find(scene.objects[i].id);
                if (owner == scene.attractions.end() || owner->second != ref)
                    continue;
                const auto model = scene.models.find(park::model_ref(scene.objects[i]));
                ui::SceneryFacts f;
                f.colours = scene.objects[i].colours;
                f.colour_slots = model == scene.models.end() ? 0 : park::colour_slots(model->second);
                if (std::vector<std::size_t> slots = ui::scenery_paint_slots(f); !slots.empty())
                    return std::pair { i, std::move(slots) };
            }
            return std::nullopt;
        }

        ui::AttractionFacts facts(const ParkScene& scene, park::AttractionRef ref)
        {
            ui::AttractionFacts f { .guests = scene.people.guests, .refusal = scene.refusal };
            if (ref.kind == park::AttractionRef::Kind::Stall)
            {
                if (ref.index < scene.rides.stalls.size())
                {
                    f.stall = &scene.rides.stalls[ref.index];
                    f.common = &f.stall->base;
                }
            }
            else if (ref.index < scene.rides.rides.size())
            {
                f.ride = &scene.rides.rides[ref.index];
                f.common = &f.ride->base;
                f.queue_signs = !scene.led_signs.hidden.contains(f.ride->base.id);
                f.seats = flat_view(scene, ref.index, false, 0) || flat_view(scene, ref.index, true, 0);
                f.music = scene.tunes;
                if (const auto tune = scene.ride_tunes.find(f.ride->base.id); tune != scene.ride_tunes.end())
                    f.tune = tune->second;
                f.low_friction = scene.low_friction.contains(f.ride->base.id);
                if (const auto series = scene.graph_series.find(f.ride->base.id); series != scene.graph_series.end())
                    f.graph_series = series->second;
                const auto show = std::ranges::find(scene.shows, f.ride->base.id, &save::RideShow::ride);
                f.show = show == scene.shows.end() ? nullptr : &*show;
                if (f.ride->kind == save::RideKind::Tracked)
                {
                    const auto& course = ref.index < scene.courses.size() ? scene.courses[ref.index] : std::nullopt;
                    f.station = !f.ride->stations.empty() || (course && std::ranges::contains(course->kinds, park::CourseKind::Station));
                    // Its trains can change until it joins the running park.
                    // shortcut: cars are not held to what its station's length takes, upgrade with stations that
                    // trains stop along.
                    const auto limits = scene.train_limits.find(ref.index);
                    const auto fits = scene.car_fits.find(ref.index);
                    if (limits != scene.train_limits.end() && fits != scene.car_fits.end())
                    {
                        const auto choices = scene.train_choices.find(ref.index);
                        f.other_cars = choices != scene.train_choices.end() && choices->second.size() > 1;
                        f.trains = limits->second;
                        f.trains->most_trains = course ? park::trains_fitting(*course, fits->second) : 1;
                        // Kept a block apart, it takes one train fewer than it has blocks: each ends at a station,
                        // lift or block brake.
                        if (course && f.ride->options.block_sectioned && course->kinds.size() > 1)
                        {
                            const auto ends_block = [](park::CourseKind k) { return k != park::CourseKind::Plain && k != park::CourseKind::Launch && k != park::CourseKind::Brake; };
                            const std::size_t last = course->kinds.size() - 1; // The last kind is past the track's end
                            int blocks = 0;
                            for (std::size_t i = 0; i < last; ++i)
                            {
                                const park::CourseKind next = i + 1 < last ? course->kinds[i + 1] : course->kinds.front();
                                blocks += ends_block(course->kinds[i]) && next != course->kinds[i] ? 1 : 0;
                            }
                            f.trains->most_trains = std::min(f.trains->most_trains, std::max(blocks - 1, 1));
                        }
                    }
                }
            }
            // A running ride's seats, queue and riders an hour.
            if (f.ride != nullptr && scene.simulate && ref.index < scene.ride_entities.size() && scene.ride_entities[ref.index] != entt::null)
            {
                if (const auto* cycle = scene.sim.registry.try_get<simulation::CycleRide>(scene.ride_entities[ref.index]))
                {
                    f.flow = ui::RideFlow { cycle->capacity, cycle->queue.size(), cycle->per_hour, cycle->riders.size() };
                    // A tracked ride's riders are aboard its trains.
                    if (const auto* fleet = scene.sim.registry.try_get<simulation::Trains>(scene.ride_entities[ref.index]))
                        for (const auto& aboard : fleet->riders)
                            f.flow->riding += aboard.size();
                }
            }
            if (const auto painted = painted_object(scene, ref))
            {
                for (const std::size_t slot : painted->second)
                    f.colours.push_back(static_cast<std::size_t>(std::max(scene.objects[painted->first].colours.at(slot), 0)));
            }
            return f;
        }

        save::AttractionCommon* attraction_common(ParkScene& scene, park::AttractionRef ref)
        {
            if (ref.kind == park::AttractionRef::Kind::Stall)
                return ref.index < scene.rides.stalls.size() ? &scene.rides.stalls[ref.index].base : nullptr;
            return ref.index < scene.rides.rides.size() ? &scene.rides.rides[ref.index].base : nullptr;
        }

        /// What the saved worker's window shows of `staff`'s pay, training and mood.
        void copy_mood(const simulation::Staff& staff, save::Staff& saved)
        {
            saved.wage_level = staff.wage_level;
            saved.training = staff.training;
            saved.warnings = staff.warnings;
            saved.happiness = staff.happiness.value;
            saved.laziness = staff.laziness.value;
        }

        ui::PersonFacts person_facts(const ParkScene& scene, std::size_t shown)
        {
            ui::PersonFacts f;
            f.people = &scene.people;
            f.rides = &scene.rides;
            if (shown >= scene.shown_people.size())
                return f;
            const ShownPerson& p = scene.shown_people[shown];
            if (p.staff && p.saved < scene.people.staff.size())
                f.staff = &scene.people.staff[p.saved];
            else if (!p.staff && p.saved < scene.people.guests.size())
                f.guest = &scene.people.guests[p.saved];
            if (const entt::entity e = shown_staff(scene); e != entt::null)
            {
                const auto* patrol = scene.sim.registry.try_get<simulation::Patrol>(e);
                f.patrol_tiles = patrol != nullptr ? patrol->tiles.size() : 0;
                f.painting_patrol = scene.painting_patrol;
                f.patrol_brush = scene.patrol_brush;
                f.patrol_zone = patrol != nullptr ? patrol->zone : 0;
                const auto& staff = scene.sim.registry.get<simulation::Staff>(e);
                f.uncovered_paths = simulation::uncovered_paths(scene.sim, staff.role);
                if (staff.role == simulation::StaffRole::Handyman)
                    f.items_mended = staff.mended;
            }
            return f;
        }

        ui::InfoUnits info_units(const ui::InterfaceAssets& assets, const config::GameOptions& options)
        {
            ui::InfoUnits units;
            units.currency = ui::currency(*assets.strings, options.choice(config::GameOption::Currency));
            units.distance = static_cast<ui::DistanceUnits>(options.choice(config::GameOption::DistanceUnits));
            return units;
        }

        ui::SceneryFacts prop_facts(const ParkScene& scene, std::size_t index)
        {
            ui::SceneryFacts f;
            if (index >= scene.objects.size())
                return f;
            const save::PlacedObject& o = scene.objects[index];
            f.symbol = o.symbol;
            f.colours = o.colours;
            if (const auto model = scene.models.find(park::model_ref(o)); model != scene.models.end())
                f.colour_slots = park::colour_slots(model->second);
            if (const auto bin = std::ranges::find(scene.bins, o.id, &save::BinState::object_ref); bin != scene.bins.end())
                f.bin = &*bin;
            if (const auto sign = std::ranges::find(scene.signs, o.id, &save::SignState::object_ref); sign != scene.signs.end())
                f.sign = &*sign;
            return f;
        }

        void show_info_page(ParkScene& scene, ui::InfoPanel& panel, const ui::InterfaceAssets& assets, const config::GameOptions& options)
        {
            if (scene.shown_prop)
                panel.show_page(ui::scenery_page(scene.prop_tab, prop_facts(scene, *scene.shown_prop), *assets.strings));
            else if (scene.shown_person)
                panel.show_page(ui::person_page(scene.person_tab, person_facts(scene, *scene.shown_person), *assets.strings, info_units(assets, options)));
            else if (scene.shown)
                panel.show_page(ui::attraction_page(scene.info_tab, facts(scene, *scene.shown), *assets.strings, info_units(assets, options), scene.info_section));
        }

        /// The round view of a person: looking at them from where the player looks from, turning from there.
        park::ParkCamera person_view(const ParkScene& scene, std::size_t shown, float yaw)
        {
            park::ParkCamera camera;
            camera.target = glm::vec3(scene.shown_people[shown].pose.world[3]) + glm::vec3(0, kPersonViewHeight, 0);
            camera.yaw = yaw;
            camera.pitch = kViewPitch;
            camera.distance = kPersonViewDistance;
            return camera;
        }

        /// Opens the panel on the person `shown` (of the shown people), at the tab last opened on a person if
        /// they have it.
        void open_person(ParkScene& scene, ui::InfoPanel& panel, std::size_t shown, const ui::InterfaceAssets& assets, const config::GameOptions& options)
        {
            if (shown >= scene.shown_people.size())
                return;
            std::vector<ui::InfoTab> tabs;
            scene.person_tabs.clear();
            std::size_t picked = 0;
            const ui::PersonFacts f = person_facts(scene, shown);
            for (const ui::PersonTabInfo& t : ui::person_tabs(f))
            {
                if (t.tab == scene.person_tab)
                    picked = tabs.size();
                ui::InfoTab& tab = tabs.emplace_back();
                tab.icon = t.icon;
                tab.help = assets.strings->get(t.help);
                tab.slot = t.slot;
                // A uniform's pots show the colours the person is drawn in.
                for (std::size_t pot = 0; pot < t.pots; ++pot)
                    tab.paints.push_back(scene.shown_people[shown].pose.look.colours.at(ui::kUpperPalette + pot));
                tab.colours = ui::kUniformColours;
                scene.person_tabs.push_back(t.tab);
            }
            // A uniform tab has no page of its own.
            if (scene.person_tabs.at(picked) == ui::PersonTab::Uniform)
                picked = 0;
            scene.person_tab = scene.person_tabs.at(picked);
            if (scene.shown_person != shown)
            {
                scene.view_camera = person_view(scene, shown, scene.camera.yaw);
                scene.painting_patrol = false;
            }
            scene.shown.reset();
            scene.shown_prop.reset();
            scene.shown_person = shown;
            panel.show(std::move(tabs), picked);
            show_info_page(scene, panel, assets, options);
        }

        /// Opens the panel on the attraction `ref`, at `tab`, else the tab last opened, if it has one.
        void open_info(ParkScene& scene, ui::InfoPanel& panel, park::AttractionRef ref, const ui::InterfaceAssets& assets, const config::GameOptions& options, std::optional<ui::AttractionTab> tab = std::nullopt)
        {
            const ui::AttractionFacts f = facts(scene, ref);
            if (f.common == nullptr)
                return;
            const ui::AttractionKind kind = f.stall ? ui::AttractionKind::Stall : ui::attraction_kind(*f.ride);
            if (const bool tracked = f.ride != nullptr && f.ride->kind == save::RideKind::Tracked; tracked != scene.info_tracked)
            {
                std::swap(scene.info_tab, scene.other_info_tab);
                scene.info_tracked = tracked;
            }
            if (tab)
                scene.info_tab = *tab;
            std::vector<ui::InfoTab> tabs;
            scene.shown_tabs.clear();
            std::size_t picked = 0;
            for (const ui::AttractionTabInfo& t : ui::attraction_tabs(kind, f.common->state))
            {
                if (t.tab == scene.info_tab)
                    picked = tabs.size();
                ui::InfoTab& tab = tabs.emplace_back();
                tab.icon = t.icon;
                tab.help = assets.strings->get(t.help);
                tab.slot = t.slot;
                tab.action = t.action;
                if (t.tab == ui::AttractionTab::State)
                {
                    for (const ui::StateChoice& c : ui::state_choices(kind))
                    {
                        const bool now = c.state == f.common->state;
                        tab.choices.push_back({ std::string(now ? c.disabled_icon : c.icon), std::u16string(assets.strings->get(c.help)), !now });
                    }
                }
                scene.shown_tabs.push_back(t.tab);
            }
            // The open-or-close tab has no page of its own.
            if (scene.shown_tabs.at(picked) == ui::AttractionTab::State)
                picked = 0;
            scene.info_tab = scene.shown_tabs.at(picked);
            if (scene.shown != ref || scene.shown_person)
            {
                // The view starts from where the player looks from, and turns from there.
                std::vector<glm::vec3> points;
                for (std::size_t i = 0; i < scene.objects.size() && i < scene.anchors.size(); ++i)
                {
                    const auto owner = scene.attractions.find(scene.objects[i].id);
                    if (owner != scene.attractions.end() && owner->second == ref)
                        points.push_back(scene.anchors[i]);
                }
                scene.view_camera = park::framed(points, scene.camera.yaw, kViewPitch);
            }
            scene.shown_person.reset();
            scene.shown_prop.reset();
            if (scene.shown != ref)
                scene.refusal.clear();
            scene.shown = ref;
            panel.show(std::move(tabs), picked);
            show_info_page(scene, panel, assets, options);
        }

        /// Opens the panel on the prop `index` (of the objects), with only its name showing.
        void open_prop(ParkScene& scene, ui::InfoPanel& panel, std::size_t index, const ui::InterfaceAssets& assets, const config::GameOptions& options)
        {
            if (index >= scene.objects.size())
                return;
            std::vector<ui::InfoTab> tabs;
            scene.prop_tabs.clear();
            for (const ui::SceneryTabInfo& t : ui::scenery_tabs(prop_facts(scene, index)))
            {
                ui::InfoTab& tab = tabs.emplace_back();
                tab.icon = t.icon;
                tab.help = assets.strings->get(t.help);
                tab.slot = t.slot;
                tab.action = t.action;
                tab.paints = t.paints;
                scene.prop_tabs.push_back(t.tab);
            }
            if (scene.shown_prop != index)
                scene.view_camera = park::framed(park::object_corners(scene.objects[index], scene.models, scene.grid, scene.fitted), scene.camera.yaw, kViewPitch, kPropViewClosest);
            scene.shown.reset();
            scene.shown_person.reset();
            scene.shown_prop = index;
            scene.prop_tab.reset();
            const std::size_t none = tabs.size();
            panel.show(std::move(tabs), none);
            show_info_page(scene, panel, assets, options);
        }

        /// Changes the object `index` by `change`. The objects' meshes are built beside the frame from the
        /// objects, so the build in hand is dropped first and another started with the change.
        template<typename Change> void change_object(render::Device& device, ParkScene& scene, std::size_t index, Change&& change)
        {
            if (index >= scene.objects.size())
                return;
            if (scene.object_build.valid())
                (void)scene.object_build.get();
            change(scene.objects[index]);
            // Scenery its bones move is built again, in its new colours or not at all.
            if (const auto boned = std::ranges::find_if(scene.boned_rides, [&](const ParkScene::BonedRide& b) { return b.object == index && b.ride == entt::null; }); boned != scene.boned_rides.end())
            {
                scene.boned_rides.erase(boned);
                animate_scenery(device, scene, index);
            }
            scene.levels = { };
            scene.since_rebuild = kObjectRebuildInterval;
        }

        /// Takes the prop `index` out of the park.
        void destroy_prop(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, std::size_t index)
        {
            if (index >= scene.objects.size())
                return;
            const auto wall = park::edge_wall(scene.objects[index], scene.models, scene.grid);
            change_object(device, scene, index, [](save::PlacedObject& o) { o.hidden = true; });
            // Guests stop using what stood on the path, and the park keeps no bin or sign for it.
            const std::uint64_t id = scene.objects[index].id;
            auto& r = scene.sim.registry;
            std::vector<entt::entity> gone;
            for (const auto [e, item] : r.view<const simulation::PathItem>().each())
                if (item.object == id)
                    gone.push_back(e);
            r.destroy(gone.begin(), gone.end());
            std::erase_if(scene.bins, [&](const save::BinState& b) { return b.object_ref == id; });
            std::erase_if(scene.signs, [&](const save::SignState& s) { return s.object_ref == id; });
            if (wall)
                wall_changed(device, scene, assets, *wall, false);
        }

        /// Paints colour slot `slot` of the prop `index` in colour `colour` of the 32.
        void paint_prop(render::Device& device, ParkScene& scene, std::size_t index, std::size_t slot, std::int32_t colour)
        {
            if (slot < park::kFlexiSlotCount)
                change_object(device, scene, index, [&](save::PlacedObject& o) { o.colours.at(slot) = colour; });
        }

        /// Of the cars of ride `ride`, those with a camera of kind `eye`, in the order the park keeps them.
        std::vector<std::size_t> ride_seats(const ParkScene& scene, std::size_t ride, park::RideEye eye)
        {
            std::vector<std::size_t> seats;
            for (std::size_t i = 0; i < scene.cars.size() && i < scene.car_spots.size(); ++i)
                if (scene.car_spots[i].ride == ride && ride_eye(scene, i, eye))
                    seats.push_back(i);
            return seats;
        }

        /// The kinds of camera after `eye`, round from the rear to the nose, that the train car `car` is on has.
        std::optional<park::RideEye> next_eye(const ParkScene& scene, std::size_t car, park::RideEye eye)
        {
            for (std::uint8_t step = 1; step < 3; ++step)
            {
                const auto other = static_cast<park::RideEye>((std::to_underlying(eye) + step) % 3);
                if (std::ranges::any_of(ride_seats(scene, scene.car_spots[car].ride, other), [&](std::size_t c) { return scene.car_spots[c].train == scene.car_spots[car].train; }))
                    return other;
            }
            return std::nullopt;
        }

        /// Every train of the park a view can ride along in, ride by ride in the park's order: the first car of
        /// each with a camera of kind `eye`, or on a ride with none of that kind, of the first kind it has of the
        /// front seat, rear seat and nose.
        std::vector<std::pair<std::size_t, park::RideEye>> train_stops(const ParkScene& scene, park::RideEye eye)
        {
            std::vector<std::pair<std::size_t, park::RideEye>> stops;
            for (std::size_t ride = 0; ride < scene.rides.rides.size(); ++ride)
                for (const park::RideEye kind : { eye, park::RideEye::Front, park::RideEye::Rear, park::RideEye::Nose })
                    if (const std::vector<std::size_t> seats = ride_seats(scene, ride, kind); !seats.empty())
                    {
                        std::vector<std::size_t> trains;
                        for (const std::size_t seat : seats)
                            if (!std::ranges::contains(trains, scene.car_spots[seat].train))
                            {
                                trains.push_back(scene.car_spots[seat].train);
                                stops.emplace_back(seat, kind);
                            }
                        break;
                    }
            return stops;
        }

        /// Unless already riding: everything else on screen is put away, the ride camera panel shows, and a park
        /// that was paused is set going so the ride moves.
        void start_riding(ParkScene& scene, const ui::InterfaceAssets& assets)
        {
            if (scene.riding_along())
                return;
            ui::Widget& root = scene.desktop->root();
            scene.hidden_for_ride.clear();
            for (const auto& child : root.children())
                if (child->visible && child->name() != kGuideName)
                {
                    child->visible = false;
                    scene.hidden_for_ride.emplace_back(child->name());
                }
            root.add<ui::RideCameraPanel>(std::string(kRideCameraName), skins::ride_camera_panel_style(assets.palette), *assets.strings);
            scene.paused_for_ride = scene.park.speed.paused;
            scene.park.speed.paused = false;
        }

        /// How many rider seats flat ride `ride` has to look out from, or with `fixed` fixed cameras.
        std::size_t flat_views(const ParkScene& scene, std::size_t ride, bool fixed)
        {
            std::size_t count = 0;
            while (flat_view(scene, ride, fixed, count))
                ++count;
            return count;
        }

        /// The flat rides with rider seats or fixed cameras to look out from, in the park's order.
        std::vector<std::size_t> flat_rides(const ParkScene& scene)
        {
            std::vector<std::size_t> rides;
            for (std::size_t ride = 0; ride < scene.rides.rides.size(); ++ride)
                if (flat_view(scene, ride, false, 0) || flat_view(scene, ride, true, 0))
                    rides.push_back(ride);
            return rides;
        }

        /// Looks out from rider seat `at` of flat ride `ride`, or with `fixed` from its fixed camera `at`, as
        /// ride_along does from a car. The type button changes between seats and fixed cameras, the train
        /// buttons go from flat ride to flat ride.
        void ride_flat(ParkScene& scene, const ui::InterfaceAssets& assets, std::size_t ride, bool fixed, std::size_t at)
        {
            start_riding(scene, assets);
            if (scene.riding_flat != std::pair { ride, at } || scene.riding_fixed != fixed)
            {
                scene.ride_turn = { };
                scene.ride_pull = 0;
            }
            scene.riding.reset();
            scene.riding_person.reset();
            scene.riding_flat = { ride, at };
            scene.riding_fixed = fixed;
            const ui::StringTable& strings = *assets.strings;
            constexpr std::array<std::string_view, 2> kNames { "RidecamGUI_TypePeep", "RidecamGUI_TypeStatic" };
            std::u16string info = ui::attraction_name(scene.rides.rides[ride].base, strings);
            info += u"  " + std::u16string(strings.get(kNames[fixed])) + u" " + strings::to_utf16(std::format("{}", at + 1));
            const bool other = flat_views(scene, ride, !fixed) > 0;
            const std::optional<std::size_t> next = other ? std::optional<std::size_t>(fixed ? ui::kFlatSeatType : ui::kFlatFixedType) : std::nullopt;
            if (auto panel = scene.desktop->root().find_as<ui::RideCameraPanel>(kRideCameraName))
                panel->show(std::move(info), flat_views(scene, ride, fixed) > 1, flat_rides(scene).size() > 1, strings.get("RidecamGUI_Ride"), next, other ? strings.get(kNames[!fixed]) : std::u16string_view { });
        }

        /// Goes with person `person` of `shown_people`, from their eyes or with `behind` from behind them, as
        /// ride_along does in a car; the train buttons go from person to person, the type button changes between
        /// the two views.
        void ride_person(ParkScene& scene, const ui::InterfaceAssets& assets, std::size_t person, bool behind)
        {
            if (person >= scene.shown_people.size())
                return;
            start_riding(scene, assets);
            if (scene.riding_person != person || scene.person_behind != behind)
            {
                scene.ride_turn = { };
                scene.ride_pull = 0;
            }
            scene.riding.reset();
            scene.riding_flat.reset();
            scene.riding_person = person;
            scene.person_behind = behind;
            const ShownPerson& p = scene.shown_people[person];
            const save::Person* saved = p.staff ? (p.saved < scene.people.staff.size() ? &scene.people.staff[p.saved].person : nullptr) : (p.saved < scene.people.guests.size() ? &scene.people.guests[p.saved].person : nullptr);
            const ui::StringTable& strings = *assets.strings;
            constexpr std::array<std::string_view, 2> kNames { "RidecamGUI_TypePeep", "RidecamGUI_TypeStatic" };
            if (auto panel = scene.desktop->root().find_as<ui::RideCameraPanel>(kRideCameraName))
                panel->show(saved != nullptr ? strings::to_utf16(saved->name) : std::u16string { }, false, scene.shown_people.size() > 1, { }, behind ? ui::kFlatSeatType : ui::kFlatFixedType, strings.get(kNames[!behind]));
        }

        /// Rides along in `car`, from its camera of kind `eye`.
        void ride_along(ParkScene& scene, const ui::InterfaceAssets& assets, std::size_t car, park::RideEye eye)
        {
            ui::Widget& root = scene.desktop->root();
            start_riding(scene, assets);
            if (scene.riding != car || scene.riding_eye != eye)
            {
                scene.ride_view = { };
                scene.ride_turn = { };
                scene.ride_pull = 0;
            }
            scene.riding_flat.reset();
            scene.riding_person.reset();
            scene.riding = car;
            scene.riding_eye = eye;
            // Named as the game names them: the ride, then the train and the car counting from 1.
            const park::CarSpot spot = scene.car_spots[car];
            std::size_t place = 1;
            for (std::size_t c = 0; c < car; ++c)
                place += scene.car_spots[c].ride == spot.ride && scene.car_spots[c].train == spot.train ? 1 : 0;
            const ui::StringTable& strings = *assets.strings;
            std::u16string info = ui::attraction_name(scene.rides.rides[spot.ride].base, strings);
            info += u"  " + std::u16string(strings.get("RidecamGUI_Train")) + u" " + strings::to_utf16(std::format("{}", spot.train + 1));
            info += u"  " + std::u16string(strings.get("RidecamGUI_Car")) + u" " + strings::to_utf16(std::format("{}", place));
            constexpr std::array<std::string_view, 3> kEyeNames { "RidecamGUI_TypeNose", "RidecamGUI_TypeFront", "RidecamGUI_TypeRear" };
            info += u"  " + std::u16string(strings.get(kEyeNames[std::to_underlying(eye)]));
            const auto cameras = std::ranges::count_if(ride_seats(scene, spot.ride, eye), [&](std::size_t seat) { return scene.car_spots[seat].train == spot.train; });
            const std::optional<park::RideEye> next = next_eye(scene, car, eye);
            if (auto panel = root.find_as<ui::RideCameraPanel>(kRideCameraName))
                panel->show(std::move(info), cameras > 1, train_stops(scene, eye).size() > 1, strings.get("RidecamGUI_Train"), next.transform([](park::RideEye e) { return std::size_t { std::to_underlying(e) }; }), next ? strings.get(kEyeNames[std::to_underlying(*next)]) : std::u16string_view { });
        }

        void stop_riding(ParkScene& scene);

        constexpr float kRidePullPerNotch = 0.5f; ///< Of the ride camera's pull back, per wheel notch
        constexpr float kRidePullMost = 3;        ///< The most it pulls back, which is squared into metres

        /// Rides along on ride `ride` (of the park's rides): a flat ride from its first rider seat; a tracked ride
        /// from the first front seat camera of a train under way, else of the first train, the rear or nose
        /// camera where the cars have no front one. Nothing when it has neither.
        void ride_cam(ParkScene& scene, const ui::InterfaceAssets& assets, std::size_t ride)
        {
            if (flat_view(scene, ride, false, 0) || flat_view(scene, ride, true, 0))
            {
                ride_flat(scene, assets, ride, !flat_view(scene, ride, false, 0), 0);
                return;
            }
            const entt::entity e = ride < scene.ride_entities.size() ? scene.ride_entities[ride] : entt::null;
            const auto* fleet = e != entt::null ? scene.sim.registry.try_get<simulation::Trains>(e) : nullptr;
            for (const park::RideEye eye : { park::RideEye::Front, park::RideEye::Rear, park::RideEye::Nose })
                if (const std::vector<std::size_t> seats = ride_seats(scene, ride, eye); !seats.empty())
                {
                    const auto running = std::ranges::find_if(seats, [&](std::size_t car) { return fleet != nullptr && scene.car_spots[car].train < fleet->trains.size() && fleet->trains[scene.car_spots[car].train].running; });
                    ride_along(scene, assets, running != seats.end() ? *running : seats.front(), eye);
                    return;
                }
        }

        /// Leaves, or changes to the train's first camera of the next kind it has, or steps to the previous or next
        /// camera of the same kind on the train (round from the last to the first), or to the previous or next
        /// train, on from a ride's last train to the next ride's first, round the park.
        void ride_action(ParkScene& scene, const ui::InterfaceAssets& assets, ui::RideCameraAction action)
        {
            using enum ui::RideCameraAction;
            if (action == Leave)
            {
                stop_riding(scene);
                return;
            }
            const bool next = action == NextCamera || action == NextTrain;
            const auto step = [&](std::size_t at, std::size_t count) { return (at + (next ? 1 : count - 1)) % count; };
            // With a person, the type changes between their eyes and behind them, and the trains are the other people.
            if (scene.riding_person)
            {
                if (action == ChangeType)
                    ride_person(scene, assets, *scene.riding_person, !scene.person_behind);
                else if ((action == PreviousTrain || action == NextTrain) && !scene.shown_people.empty())
                    ride_person(scene, assets, step(*scene.riding_person, scene.shown_people.size()), scene.person_behind);
                return;
            }
            // On a flat ride, the cameras are its seats or its fixed cameras, the type changes between them, and the
            // trains are the park's other flat rides, entered as ride_cam does.
            if (scene.riding_flat)
            {
                const auto [ride, view] = *scene.riding_flat;
                const bool fixed = scene.riding_fixed;
                const std::size_t views = flat_views(scene, ride, fixed);
                const std::vector<std::size_t> rides = flat_rides(scene);
                if ((action == PreviousCamera || action == NextCamera) && views > 0)
                    ride_flat(scene, assets, ride, fixed, step(view, views));
                else if (action == ChangeType && flat_views(scene, ride, !fixed) > 0)
                    ride_flat(scene, assets, ride, !fixed, 0);
                else if (const auto at = std::ranges::find(rides, ride); (action == PreviousTrain || action == NextTrain) && at != rides.end())
                {
                    const std::size_t to = rides[step(static_cast<std::size_t>(at - rides.begin()), rides.size())];
                    ride_flat(scene, assets, to, !flat_view(scene, to, false, 0), 0);
                }
                return;
            }
            if (!scene.riding)
                return;
            const park::CarSpot spot = scene.car_spots[*scene.riding];
            if (action == ChangeType)
            {
                if (const auto next = next_eye(scene, *scene.riding, scene.riding_eye))
                    for (const std::size_t seat : ride_seats(scene, spot.ride, *next))
                        if (scene.car_spots[seat].train == spot.train)
                        {
                            ride_along(scene, assets, seat, *next);
                            break;
                        }
                return;
            }
            if (action == PreviousCamera || action == NextCamera)
            {
                std::vector<std::size_t> stops = ride_seats(scene, spot.ride, scene.riding_eye);
                std::erase_if(stops, [&](std::size_t seat) { return scene.car_spots[seat].train != spot.train; });
                if (const auto at = std::ranges::find(stops, *scene.riding); at != stops.end())
                    ride_along(scene, assets, stops[step(static_cast<std::size_t>(at - stops.begin()), stops.size())], scene.riding_eye);
                return;
            }
            const std::vector<std::pair<std::size_t, park::RideEye>> stops = train_stops(scene, scene.riding_eye);
            const auto at = std::ranges::find_if(stops, [&](const auto& stop) { return scene.car_spots[stop.first].ride == spot.ride && scene.car_spots[stop.first].train == spot.train; });
            if (at != stops.end())
            {
                const auto [car, eye] = stops[step(static_cast<std::size_t>(at - stops.begin()), stops.size())];
                ride_along(scene, assets, car, eye);
            }
        }

        /// Back to the overhead view, with what was on screen before.
        void stop_riding(ParkScene& scene)
        {
            if (!scene.riding_along())
                return;
            ui::Widget& root = scene.desktop->root();
            for (const std::string& name : scene.hidden_for_ride)
                if (auto w = root.find(name))
                    w->visible = true;
            if (auto panel = root.find(kRideCameraName))
                panel->close();
            if (scene.paused_for_ride)
                scene.park.speed.paused = true;
            scene.riding.reset();
            scene.riding_flat.reset();
            scene.riding_person.reset();
            scene.seat.reset();
        }

        /// Writes a change the player made on an attraction's page into the save. Nothing is shown again, so a
        /// spinner held down keeps stepping; a page that looks different after the change is set up here.
        void change_attraction(render::Device& device, ParkScene& scene, ui::InfoPanel& panel, park::AttractionRef ref, const ui::InfoChange& change, const ui::InterfaceAssets& assets, const config::GameOptions& options)
        {
            using enum ui::AttractionControl;
            save::Ride* ride = ref.kind == park::AttractionRef::Kind::Ride && ref.index < scene.rides.rides.size() ? &scene.rides.rides[ref.index] : nullptr;
            save::Stall* stall = ref.kind == park::AttractionRef::Kind::Stall && ref.index < scene.rides.stalls.size() ? &scene.rides.stalls[ref.index] : nullptr;
            const auto wait_on = [&](std::int32_t& wait, ui::AttractionControl seconds) {
                const auto field = panel.control(ui::control_id(seconds));
                wait = change.value != 0 ? (field ? static_cast<const ui::Spinner&>(*field).value() : 0) : -1;
                if (field)
                    field->enabled = change.value != 0;
            };
            if (change.id == ui::control_id(Colours))
            {
                // Every object of the attraction takes the colour in the slot of the pot.
                const auto painted = painted_object(scene, ref);
                if (!painted || change.pot >= painted->second.size())
                    return;
                const std::size_t slot = painted->second[change.pot];
                for (std::size_t i = 0; i < scene.objects.size(); ++i)
                {
                    const auto owner = scene.attractions.find(scene.objects[i].id);
                    if (owner != scene.attractions.end() && owner->second == ref)
                        paint_prop(device, scene, i, slot, change.value);
                }
            }
            else if (change.id == ui::control_id(Name))
            {
                // A blank name keeps the old one, as in the game.
                if (save::AttractionCommon* common = ride != nullptr ? &ride->base : stall != nullptr ? &stall->base : nullptr; common != nullptr && change.text.find_first_not_of(u' ') != std::u16string::npos)
                    common->name = strings::to_utf8(change.text);
            }
            else if (change.id == ui::control_id(Locate))
            {
                // The view turns round to the middle of the attraction's objects.
                glm::vec3 sum { };
                std::size_t count = 0;
                for (const save::PlacedObject& o : scene.objects)
                {
                    const auto owner = scene.attractions.find(o.id);
                    if (owner == scene.attractions.end() || owner->second != ref)
                        continue;
                    const auto model = scene.models.find(park::model_ref(o));
                    sum += glm::vec3(park::placed_transform(o, model == scene.models.end() ? nullptr : &model->second, scene.grid, scene.fitted)[3]);
                    ++count;
                }
                if (count > 0)
                    scene.camera.target = sum / static_cast<float>(count);
            }
            else if (change.id == ui::control_id(RideCam) && ride != nullptr)
                ride_cam(scene, assets, ref.index);
            else if (stall != nullptr && change.id >= ui::control_id(ItemPrice))
            {
                if (const auto item = static_cast<std::size_t>(change.id - ui::control_id(ItemPrice)); item < stall->items.size())
                    stall->items[item].price = change.value;
            }
            else if (ride == nullptr)
                return;
            else if (change.id == ui::control_id(Price))
                ride->ticket_price = change.value;
            else if (change.id == ui::control_id(Inspection))
            {
                ride->breakdown.inspection_interval = change.value;
                if (const entt::entity e = ref.index < scene.ride_entities.size() ? scene.ride_entities[ref.index] : entt::null; e != entt::null)
                    if (auto* upkeep = scene.sim.registry.try_get<simulation::Upkeep>(e))
                        upkeep->inspect_every = simulation::upkeep_of(*ride).inspect_every;
            }
            else if (change.id == ui::control_id(Mechanic))
            {
                // shortcut: the game also opens the mechanic's window and puts this job first on a busy one's list;
                // add both with the staff window's job list.
                if (const entt::entity e = ref.index < scene.ride_entities.size() ? scene.ride_entities[ref.index] : entt::null; e != entt::null)
                    if (auto* upkeep = scene.sim.registry.try_get<simulation::Upkeep>(e))
                        upkeep->called = true;
            }
            else if (change.id == ui::control_id(MinWaitOn))
                wait_on(ride->options.min_wait, MinWait);
            else if (change.id == ui::control_id(MaxWaitOn))
                wait_on(ride->options.max_wait, MaxWait);
            else if (change.id == ui::control_id(MinWait))
                ride->options.min_wait = change.value;
            else if (change.id == ui::control_id(MaxWait))
                ride->options.max_wait = change.value;
            else if (change.id == ui::control_id(WaitFor))
                ride->options.wait_for_load = change.value;
            else if ((change.id == ui::control_id(Trains) || change.id == ui::control_id(Cars)) && scene.car_fits.contains(ref.index) && assets.install)
            {
                const bool trains = change.id == ui::control_id(Trains);
                scene.building.arm = fit_trains(scene, *assets.install, ref.index, trains ? change.value : std::max(ride->train_count, 1), trains ? std::max(ride->cars_per_train, 1) : change.value) || scene.building.arm;
                // Longer trains fit fewer times round the track.
                const auto& course = ref.index < scene.courses.size() ? scene.courses[ref.index] : std::nullopt;
                if (const int most = course ? park::trains_fitting(*course, scene.car_fits[ref.index]) : 1; ride->train_count > most)
                    fit_trains(scene, *assets.install, ref.index, most, ride->cars_per_train);
            }
            else if (change.id == ui::control_id(CarKind) && scene.car_fits.contains(ref.index) && assets.install && !ride->trains.empty())
            {
                // The next kind of train its kind runs, round to the first after the last.
                const std::vector<std::string>& choices = scene.train_choices[ref.index];
                const std::string_view symbol = ride->trains.front().symbol;
                const std::string_view now = symbol.substr(0, symbol.find(':'));
                const auto at = std::ranges::find_if(choices, [&](const std::string& c) { return strings::equals_ignoring_case(c, now); });
                if (choices.size() > 1)
                    if (const auto arm = use_train(scene, *assets.install, ref.index, choices[at == choices.end() ? 0 : static_cast<std::size_t>(at - choices.begin() + 1) % choices.size()]))
                        scene.building.arm = *arm || scene.building.arm;
            }
            else if (change.id == ui::control_id(Circuits))
            {
                // A go under way runs as many times round as the new count, or stops if it is past it.
                // shortcut: a tracked ride keeps the count but its trains go round once; run the laps once the
                // trains count them.
                ride->loop_count = change.value;
                if (const entt::entity e = ref.index < scene.ride_entities.size() ? scene.ride_entities[ref.index] : entt::null; e != entt::null)
                    if (auto* cycle = scene.sim.registry.try_get<simulation::CycleRide>(e))
                        cycle->loops = static_cast<std::uint32_t>(std::max(change.value, 1));
            }
            else if (const auto show = std::ranges::find(scene.shows, ride->base.id, &save::RideShow::ride); show != scene.shows.end() && (change.id == ui::control_id(ShowTrick) || change.id == ui::control_id(ShowDelete)))
            {
                // The ride puts the changed show on from its next go.
                const std::size_t place = std::min(scene.info_section, show->order.size());
                const std::vector<std::uint32_t> offers = ui::show_offers(*show);
                if (change.id == ui::control_id(ShowDelete) && place < show->order.size() && show->order.size() > 1)
                    show->order.erase(show->order.begin() + static_cast<std::ptrdiff_t>(place));
                else if (change.id == ui::control_id(ShowTrick) && change.value >= 0 && std::cmp_less(change.value, offers.size()))
                {
                    if (place < show->order.size())
                        show->order[place] = offers[static_cast<std::size_t>(change.value)];
                    else
                        show->order.push_back(offers[static_cast<std::size_t>(change.value)]);
                }
                stage_show(scene, *show);
            }
            else if (change.id == ui::control_id(BlockSection))
            {
                // shortcut: the game also caps the trains at one fewer than the blocks; upgrade with the trains'
                // limits counting blocks.
                ride->options.block_sectioned = change.value != 0;
                if (const entt::entity e = ref.index < scene.ride_entities.size() ? scene.ride_entities[ref.index] : entt::null; e != entt::null)
                    if (auto* fleet = scene.sim.registry.try_get<simulation::Trains>(e))
                        for (simulation::Coaster& c : fleet->trains)
                            c.blocks = ride->options.block_sectioned;
            }
            else if (change.id == ui::control_id(SyncStations))
                ride->options.sync_trains = change.value != 0; // shortcut: stored, stations do not wait for each other yet
            else if (change.id == ui::control_id(LeaveOnArrival))
                ride->options.leave_if_other_train_enters_station = change.value != 0;
            else if (change.id == ui::control_id(LowFriction))
            {
                if (change.value != 0)
                    scene.low_friction.insert(ride->base.id);
                else
                    scene.low_friction.erase(ride->base.id);
            }
            else if (change.id == ui::control_id(LaunchSpeed) || change.id == ui::control_id(LiftSpeed))
            {
                // The page shows hundredths of the player's unit.
                const float speed = static_cast<float>(change.value) / 100 / ui::speed_scale(info_units(assets, options).distance);
                if (change.id == ui::control_id(LaunchSpeed))
                {
                    ride->options.launch_speed = speed;
                    if (const entt::entity e = ref.index < scene.ride_entities.size() ? scene.ride_entities[ref.index] : entt::null; e != entt::null)
                        if (auto* fleet = scene.sim.registry.try_get<simulation::Trains>(e))
                            for (simulation::Coaster& c : fleet->trains)
                                c.launch_speed = simulation::launch_of(*ride);
                }
                else
                {
                    ride->options.lift_speed = speed;
                    if (const entt::entity e = ref.index < scene.ride_entities.size() ? scene.ride_entities[ref.index] : entt::null; e != entt::null)
                        if (auto* fleet = scene.sim.registry.try_get<simulation::Trains>(e))
                            for (simulation::Coaster& c : fleet->trains)
                                c.lift_speed = c.station_speed = speed;
                }
            }
            else if (change.id >= ui::control_id(SpeedSeries) && change.id <= ui::control_id(LateralSeries))
            {
                const auto bit = static_cast<std::uint8_t>(1u << (change.id - ui::control_id(SpeedSeries)));
                std::uint8_t& shown = scene.graph_series.try_emplace(ride->base.id, ui::kSpeedOnly).first->second;
                shown = static_cast<std::uint8_t>(change.value != 0 ? shown | bit : shown & ~bit);
            }
            else if (change.id == ui::control_id(Music))
                scene.ride_tunes[ride->base.id] = static_cast<std::size_t>(std::max(change.value, 0));
            else if (change.id == ui::control_id(QueueSigns))
            {
                // The button turns them round.
                if (scene.led_signs.hidden.contains(ride->base.id))
                    scene.led_signs.hidden.erase(ride->base.id);
                else
                    scene.led_signs.hidden.insert(ride->base.id);
                scene.led_signs.stale = true;
            }
            // The running ride leaves as its settings now say.
            if (ride != nullptr && ref.index < scene.ride_entities.size() && scene.ride_entities[ref.index] != entt::null)
                if (auto* departure = scene.sim.registry.try_get<simulation::Departure>(scene.ride_entities[ref.index]))
                    *departure = simulation::departure_of(ride->options);
        }

        /// Lets the people's meshes be changed: the build beside the frame reads them, so it is dropped first,
        /// and all are built again after.
        void change_people(ParkScene& scene)
        {
            if (scene.people_build.valid())
                (void)scene.people_build.get();
            if (scene.moving_build.valid())
                (void)scene.moving_build.get();
            if (scene.riders_build.valid())
                (void)scene.riders_build.get();
            scene.people_levels.clear();
            scene.since_people_levels = kObjectRebuildInterval;
        }

        /// Fires the member of staff shown as `shown`: they leave the park at once, as in the game.
        void fire(ParkScene& scene, std::size_t shown)
        {
            change_people(scene);
            const std::size_t saved = scene.shown_people.at(shown).saved;
            if (const entt::entity walker = scene.shown_people[shown].guest; walker != entt::null)
                scene.sim.registry.destroy(walker);
            scene.shown_people.erase(scene.shown_people.begin() + static_cast<std::ptrdiff_t>(shown));
            scene.people.staff.erase(scene.people.staff.begin() + static_cast<std::ptrdiff_t>(saved));
            for (ShownPerson& p : scene.shown_people)
            {
                if (p.staff && p.saved > saved)
                    --p.saved;
            }
        }

        /// After a person of `shown_people` is renamed `name`: the name the game keeps for its guest camera cheat
        /// puts the view with them, from their eyes.
        void name_cheats(ParkScene& scene, const ui::InterfaceAssets& assets, std::size_t shown, std::u16string_view name)
        {
            if (strings::equals_ignoring_case(strings::to_utf8(name), strings::to_utf8(assets.strings->get("CheatPeepName_PeepCam"))))
                ride_person(scene, assets, shown, false);
        }

        /// Writes a change the player made on a person's page into the save. Returns whether the page is to be
        /// shown again for its new look.
        bool change_person(ParkScene& scene, std::size_t shown, const ui::InfoChange& change)
        {
            using enum ui::PersonControl;
            if (shown >= scene.shown_people.size())
                return false;
            const ShownPerson& p = scene.shown_people[shown];
            save::Staff* staff = p.staff && p.saved < scene.people.staff.size() ? &scene.people.staff[p.saved] : nullptr;
            save::Guest* guest = !p.staff && p.saved < scene.people.guests.size() ? &scene.people.guests[p.saved] : nullptr;
            save::Person* person = staff != nullptr ? &staff->person : guest != nullptr ? &guest->person : nullptr;
            if (person == nullptr)
                return false;
            if (change.id == ui::person_control(Name))
            {
                // A blank name keeps the old one.
                if (change.text.empty())
                    return true;
                person->name = strings::to_utf8(change.text);
                return true;
            }
            if (staff == nullptr)
                return false;
            if (change.id == ui::person_control(PatrolPaint))
            {
                scene.painting_patrol = change.value != 0;
                return false;
            }
            if (const entt::entity e = shown_staff(scene); e != entt::null && change.id == ui::person_control(PatrolClear))
            {
                simulation::clear_patrol(scene.sim.registry, e);
                return true;
            }
            else if (e != entt::null && change.id == ui::person_control(PatrolFill))
            {
                simulation::fill_patrol(scene.sim.registry, e);
                return true;
            }
            else if (e != entt::null && change.id == ui::person_control(PatrolZone))
            {
                simulation::join_zone(scene.sim.registry, e, static_cast<std::uint32_t>(std::max(change.value, 0)));
                return true;
            }
            if (change.id == ui::person_control(PatrolBrush))
            {
                scene.patrol_brush = change.value;
                return false;
            }
            if (change.id == ui::person_control(PatrolCopy))
            {
                copy_patrol(scene);
                return true;
            }
            // Pay, training and warnings change the walking worker, and the saved one shows it; without the
            // simulation, the saved one alone.
            const entt::entity worker = shown_staff(scene);
            simulation::Staff loose { .wage_level = staff->wage_level, .training = staff->training, .warnings = staff->warnings, .happiness = staff->happiness, .laziness = staff->laziness };
            simulation::Staff& care = worker != entt::null ? scene.sim.registry.get<simulation::Staff>(worker) : loose;
            const auto show = [&] {
                copy_mood(care, *staff);
                return true;
            };
            if (change.id == ui::person_control(Warn))
            {
                if (care.warnings < ui::kMostWarnings)
                    simulation::warn(care);
                return show();
            }
            if (change.id == ui::person_control(Train))
            {
                // Paid for at once, free in a park without money; none when the park cannot afford it.
                const std::int64_t cost = simulation::training_cost(care);
                if (scene.park.cash && *scene.park.cash < cost)
                    return false;
                if (simulation::train(care) && scene.park.cash)
                    *scene.park.cash -= cost;
                return show();
            }
            if (change.id == ui::person_control(Wage))
            {
                simulation::set_wage_level(care, change.value);
                (void)show();
            }
            else if (change.id == ui::person_control(Costume))
                person->sub_type = static_cast<std::uint8_t>(change.value);
            else if (change.id >= ui::person_control(Task))
            {
                staff->task_flags ^= 1 << (change.id - ui::person_control(Task));
                care.tasks = staff->task_flags;
            }
            return false;
        }

        /// Paints the clothes of pot `pot` of the person shown as `shown` in colour `colour`.
        void paint_person(ParkScene& scene, std::size_t shown, std::size_t pot, std::size_t colour)
        {
            if (shown >= scene.shown_people.size() || ui::kUpperPalette + pot >= park::PersonLook { }.colours.size())
                return;
            change_people(scene);
            ShownPerson& p = scene.shown_people[shown];
            const auto c = static_cast<std::uint8_t>(colour);
            p.pose.look.colours.at(ui::kUpperPalette + pot) = c;
            save::Person* person = p.staff ? &scene.people.staff.at(p.saved).person : &scene.people.guests.at(p.saved).person;
            person->appearance.palettes.at(ui::kUpperPalette + pot) = c;
        }

        /// Whether the object is a prop the panel can tell about: one of the park's own.
        bool clickable_prop(const ParkScene& scene, const save::PlacedObject& o)
        {
            if (scene.attractions.contains(o.id) || o.fixture)
                return false;
            const auto model = scene.models.find(park::model_ref(o));
            return model != scene.models.end() && park::is_prop(model->second.category) && park::on_owned_land(o, model->second.squares, scene.grid);
        }

        /// What putting a broken path item back new costs: the price of the object it stands as.
        std::int64_t replace_price(const ParkScene& scene, std::uint64_t object)
        {
            const auto o = std::ranges::find(scene.objects, object, &save::PlacedObject::id);
            const auto model = o != scene.objects.end() ? scene.models.find(park::model_ref(*o)) : scene.models.end();
            return model != scene.models.end() ? model->second.price : 0;
        }

        ui::ManagementFacts management_facts(const ParkScene& scene)
        {
            const auto broken = simulation::replacement(scene.sim, [&](std::uint64_t object) { return replace_price(scene, object); });
            return { .park = &scene.park_model, .state = &scene.park, .people = &scene.people, .rides = &scene.rides, .broken_items = broken.items, .replace_cost = broken.cost };
        }

        /// The attraction whose saved id is `id`, as messages name their subject.
        std::optional<park::AttractionRef> attraction_with(const ParkScene& scene, std::uint64_t id)
        {
            using Kind = park::AttractionRef::Kind;
            if (const auto ride = std::ranges::find(scene.rides.rides, id, [](const save::Ride& r) { return r.base.id; }); ride != scene.rides.rides.end())
                return park::AttractionRef { Kind::Ride, static_cast<std::size_t>(ride - scene.rides.rides.begin()) };
            if (const auto stall = std::ranges::find(scene.rides.stalls, id, [](const save::Stall& s) { return s.base.id; }); stall != scene.rides.stalls.end())
                return park::AttractionRef { Kind::Stall, static_cast<std::size_t>(stall - scene.rides.stalls.begin()) };
            return std::nullopt;
        }

        /// Opens the info panel on what a management window's row is about, if the park shows it.
        void open_row(ParkScene& scene, ui::InfoPanel& panel, ui::ManagementRow row, const ui::InterfaceAssets& assets, const config::GameOptions& options)
        {
            using Kind = ui::ManagementRow::Kind;
            if (row.kind == Kind::Ride || row.kind == Kind::Stall)
            {
                open_info(scene, panel, { row.kind == Kind::Stall ? park::AttractionRef::Kind::Stall : park::AttractionRef::Kind::Ride, row.index }, assets, options);
                return;
            }
            const bool staff = row.kind == Kind::Staff;
            if (!staff && row.kind != Kind::Guest)
                return;
            const auto shown = std::ranges::find_if(scene.shown_people, [&](const ShownPerson& p) { return p.staff == staff && p.saved == row.index; });
            if (shown != scene.shown_people.end())
                open_person(scene, panel, static_cast<std::size_t>(shown - scene.shown_people.begin()), assets, options);
        }

        /// What the pointer is on: an attraction, a person of the shown people or a prop of the objects.
        struct Picked
        {
            std::optional<park::AttractionRef> attraction;
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
            const park::Ray ray = park::view_ray(scene.camera, size.x / size.y, ndc);
            // What is hidden cannot be clicked.
            if (scene.pick_boxes_of != scene.objects.size())
            {
                scene.pick_boxes = park::pick_boxes(scene.objects, scene.models, scene.grid, scene.fitted);
                scene.pick_boxes_of = scene.objects.size();
            }
            const auto object = park::pick_object(ray, scene.pick_boxes, scene.grid, [&](std::size_t i) {
                const save::PlacedObject& o = scene.objects[i];
                return scene.show_scenery && !o.hidden && (scene.attractions.contains(o.id) || o.fixture || clickable_prop(scene, o));
            });
            std::vector<glm::mat4> placements;
            if (scene.show_people)
            {
                placements.reserve(scene.shown_people.size());
                for (const ShownPerson& p : scene.shown_people)
                    placements.push_back(scene.simulate && p.guest != entt::null ? glm::translate(glm::mat4(1.0f), standing_at(scene, p)) : p.pose.world);
            }
            const auto person = park::pick_person(ray, placements, scene.grid);
            if (person && (!object || person->distance < object->distance))
                return { .attraction = std::nullopt, .person = person->index, .prop = std::nullopt };
            // A fence the park built takes the click and does nothing with it, as in the game.
            if (!object || scene.objects[object->index].fixture)
                return { };
            const auto owner = scene.attractions.find(scene.objects[object->index].id);
            if (owner != scene.attractions.end())
                return { .attraction = owner->second, .person = std::nullopt, .prop = std::nullopt };
            return { .attraction = std::nullopt, .person = std::nullopt, .prop = object->index };
        }

        /// The people as the map draws them.
        park::MapPeople map_people(const ParkScene& scene)
        {
            park::MapPeople shown;
            shown.people.reserve(scene.shown_people.size());
            for (const ShownPerson& p : scene.shown_people)
                shown.people.push_back({ { p.pose.world[3].x, p.pose.world[3].z }, p.staff });
            for (const auto [_, patrol] : scene.sim.registry.view<const simulation::Patrol>().each())
                shown.patrol_tiles.insert(shown.patrol_tiles.end(), patrol.tiles.begin(), patrol.tiles.end());
            return shown;
        }
    } // namespace

    void set_attraction_state(ParkScene& scene, save::AttractionCommon& attraction, save::AttractionState state)
    {
        attraction.state = state;
        for (auto [_, a] : scene.sim.registry.view<simulation::Attraction>().each())
            if (a.saved == attraction.id)
            {
                a.open = state == save::AttractionState::Open;
                a.testing = state == save::AttractionState::Testing;
            }
    }

    std::optional<std::string> change_ride_state(ParkScene& scene, std::size_t ride, save::AttractionState state)
    {
        save::Ride& r = scene.rides.rides.at(ride);
        const park::TrackRules rules = ride < scene.track_rules.size() ? scene.track_rules[ride] : park::TrackRules { };
        const auto course = ride < scene.courses.size() && scene.courses[ride] ? maybe(&*scene.courses[ride]) : std::nullopt;
        auto refused = park::open_refusal(r, rules, state, scene.building.ride == ride, course);
        if (!refused)
            set_attraction_state(scene, r.base, state);
        return refused;
    }

    void stage_show(ParkScene& scene, const save::RideShow& show)
    {
        for (auto [_, a, ride] : scene.sim.registry.view<simulation::Attraction, simulation::CycleRide>().each())
            if (a.saved == show.ride)
                simulation::stage_show(ride, show);
    }

    bool typing(const ParkScene& scene)
    {
        return scene.desktop && dynamic_cast<const ui::EditBox*>(as_pointer(scene.desktop->keyboard_focus())) != nullptr;
    }

    bool ripple_at(ParkScene& scene, ui::Point at, ui::Point size)
    {
        if (size.x <= 0 || size.y <= 0)
            return false;
        const glm::vec2 ndc { 2 * at.x / size.x - 1, 1 - 2 * at.y / size.y };
        return ripple_park_water(scene.water, park::view_ray(scene.camera, size.x / size.y, ndc), scene.grid);
    }

    void update_park_scene(render::Device& device, ParkScene& scene, ui::InterfaceAssets& assets, const config::GameOptions& options, const ParkInput& input, const ui::KeyboardState& keyboard, float elapsed)
    {
        // The ground is built before the models that give the rides' floors their size are read.
        if (scene.footpaths.floors_stale)
            rebuild_ground(device, scene, assets);
        update_led_signs(device, scene, assets);
        const float scale = scale_for(device, scene);
        const render::Extent extent = device.extent();
        ui::Desktop& desktop = *scene.desktop;
        desktop.layout({ 0, 0, static_cast<float>(extent.width) / scale, static_cast<float>(extent.height) / scale });

        // Escape asks to leave, as the guide's quit does, unless the question is already open: then it
        // answers it. An open build list takes it first, to empty its search and then to close; a path trail
        // going on is let go before anything closes.
        const bool asking = desktop.root().find(kQuestionName).has_value();
        ui::KeyboardState keys = keyboard;
        if (!asking && !build_list_open(desktop.root()))
        {
            const auto escape = std::ranges::remove_if(keys.presses, [](const ui::KeyPress& k) { return k.key == ui::Key::Escape; });
            auto tools = desktop.root().find_as<ui::ToolRow>(kToolsName);
            if (!escape.empty() && scene.riding_along())
                stop_riding(scene);
            else if (!escape.empty() && tools && tools->open_menu())
                tools->close_menu();
            else if (!escape.empty() && !drop_footpath_trail(scene) && !close_top_management_window(desktop))
                ask_leave(scene, *assets.strings, assets.palette);
            keys.presses.erase(escape.begin(), escape.end());
        }

        ui::PointerState pointer = input.pointer;
        pointer.position = { pointer.position.x / scale, pointer.position.y / scale };
        scene.current_fonts = &ui::fonts_for(device, assets, desktop.root().rect().width());
        const bool over_interface = profiling::section("Interface", [&] { return desktop.update(pointer, keys, { elapsed, scene.current_fonts }); });
        // Riding along, the park takes no clicks: the whole screen counts as interface.
        const bool over = over_interface || scene.riding_along();
        if (auto panel = scene.riding_along() ? desktop.root().find_as<ui::RideCameraPanel>(kRideCameraName) : std::nullopt)
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
                const float pitch = options.flag(config::GameOption::InvertRideCameraY) ? -1.0f : 1.0f;
                scene.ride_turn = park::look_round_limited(scene.ride_turn + glm::vec2(-moved.x / screen.x, -pitch * moved.y / screen.y) * park::kRideLookPerScreen);
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
            profiling::section("Camera", [&] { move_camera(scene, input, over, options.flag(config::GameOption::AutoScrollAtEdge), elapsed); });
        scene.zooming = input.zoom_in_key == input.zoom_out_key ? 0 : (input.zoom_in_key ? 1 : -1);
        auto info = desktop.root().find_as<ui::InfoPanel>(kInfoName);
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
            scene.since_hover_pick = 0;
        }
        if (info)
        {
            const profiling::Scope zone("InfoPanel");
            // A double click on a ride's round view rides along on it, as the ride camera key does.
            if (info->take_view_double_click() && !scene.riding_along() && scene.shown && scene.shown->kind == park::AttractionRef::Kind::Ride)
                ride_cam(scene, assets, scene.shown->index);
            // Pressing the park puts open colours away without picking.
            if ((pointer.down || input.right) && !over && info->painting())
                info->close_palette();
            // A stroke paints the patrol area while the left button is held over the park; its page shows the area
            // again when the stroke ends.
            const ui::Rect view = desktop.root().rect();
            if (paint_patrol(scene, pointer.position, { view.width(), view.height() }, pointer.down && !input.right && !input.middle && (scene.patrol_stroke || !over)))
                show_info_page(scene, *info, assets, options);
            // A stroke over the ground while the path panel shows its controls lays paths on every tile it spans.
            const bool going = !scene.footpaths.brush.empty() || scene.footpaths.slope_press.has_value();
            footpath_drag(scene, pointer.position, { view.width(), view.height() }, pointer.down && !input.middle && (going || !over), over, input.right, input.shift, input.control);
            update_ride_placing(device, scene, assets, pointer.position, { view.width(), view.height() }, over, input.shift, input.commands);
            update_land_tools(device, scene, assets, pointer.position, { view.width(), view.height() }, pointer.down && !input.right && !input.middle, over, elapsed);
            // A click is the left button pressed and let go over the park without the pointer going far.
            if (pointer.down && !scene.press && !over && !input.right && !input.middle)
                scene.press = pointer.position;
            else if (!pointer.down && scene.press)
            {
                const ui::Point from = *std::exchange(scene.press, std::nullopt);
                const bool still = std::abs(pointer.position.x - from.x) <= kClickSlack && std::abs(pointer.position.y - from.y) <= kClickSlack;
                const ui::Rect screen = desktop.root().rect();
                // While the path panel shows its controls, a click on the ground picks where the path starts.
                // While painting a patrol area, the left button paints and does not pick.
                if (painting_patrol(scene) || land_tool(scene))
                    ;
                else if (still && !over && (ride_placing_click(device, scene, assets) || track_building_click(scene, assets)))
                    ;
                else if (still && !over && !footpath_click(scene, pointer.position, { screen.width(), screen.height() }, input.control))
                {
                    const Picked picked = picked_at(scene, pointer.position, { screen.width(), screen.height() });
                    if (picked.person)
                        open_person(scene, *info, *picked.person, assets, options);
                    else if (picked.attraction)
                        open_info(scene, *info, *picked.attraction, assets, options);
                    else if (picked.prop)
                        open_prop(scene, *info, *picked.prop, assets, options);
                    else
                        ripple_at(scene, pointer.position, { screen.width(), screen.height() });
                }
            }
            if (input.right || input.middle)
                scene.press.reset();
            // A right click on a path while the path panel shows its controls sells it.
            if (input.right && !scene.right_press && !over)
                scene.right_press = pointer.position;
            else if (!input.right && scene.right_press)
            {
                const ui::Point from = *std::exchange(scene.right_press, std::nullopt);
                const ui::Rect screen = desktop.root().rect();
                if (ride_placing(scene))
                    stop_ride_placing(scene); // A right click puts the placing away, as in the game
                else if (land_tool(scene))
                    stop_land_tools(scene);
                else if (std::abs(pointer.position.x - from.x) <= kClickSlack && std::abs(pointer.position.y - from.y) <= kClickSlack && !over)
                    (void)footpath_sell(scene, pointer.position, { screen.width(), screen.height() });
            }
            // The customers page follows the running park; it has no controls to lose a half-made change in.
            scene.since_info_page += elapsed;
            if (scene.shown && scene.simulate && scene.info_tab == ui::AttractionTab::Customers && scene.since_info_page >= kLivePageInterval)
            {
                scene.since_info_page = 0;
                show_info_page(scene, *info, assets, options);
            }
            const auto action = info->take_action();
            if (action && scene.shown_prop && *action < scene.prop_tabs.size() && scene.prop_tabs[*action] == ui::SceneryTab::Destroy)
            {
                // Pulled down at once, as in the game; the panel goes with it.
                destroy_prop(device, scene, assets, *scene.shown_prop);
                scene.shown_prop.reset();
                info->hide();
            }
            if (action && scene.shown && *action < scene.shown_tabs.size() && scene.shown_tabs[*action] == ui::AttractionTab::Construction)
                scene.construct_requested = *scene.shown;
            const auto door = [](const ui::InfoChange& c) { return c.id == ui::control_id(ui::AttractionControl::Entrance) || c.id == ui::control_id(ui::AttractionControl::Exit); };
            // A tracked ride's entrance or exit goes beside its station, placed as its kind's item has it.
            const auto start_door = [&](std::size_t ride, bool exit) {
                const auto items = scene.build.items.valid() && scene.build.items.wait_for(std::chrono::seconds(0)) == std::future_status::ready ? std::span(scene.build.items.get()) : std::span<const park::BuildItem> { };
                std::string overlay = scene.rides.rides.at(ride).base.overlay_path;
                std::ranges::replace(overlay, '\\', '/');
                const auto kind = std::ranges::find_if(items, [&](const park::BuildItem& i) { return !i.entrance.empty() && strings::equals_ignoring_case(i.overlay, overlay); });
                const auto any = std::ranges::find_if(items, [](const park::BuildItem& i) { return !i.entrance.empty(); });
                const auto item = kind != items.end() ? kind : any;
                return item != items.end() && start_door_placing(scene, ride, *item, station_pieces(scene, ride, assets), exit);
            };
            // A closed circuit goes on to its doors, the entrance first, while its window shows, as in the game.
            if (const auto ride = std::exchange(scene.doors_requested, std::nullopt); ride && *ride < scene.rides.rides.size())
            {
                const auto& stations = scene.rides.rides[*ride].stations;
                const bool entrance = !stations.empty() && stations.front().entrance_spot.has_value();
                if ((!entrance || stations.front().exit_spot == std::nullopt) && start_door(*ride, entrance))
                    stop_land_tools(scene);
            }
            if (const auto change = info->take_change(); change && scene.shown && scene.shown->kind == park::AttractionRef::Kind::Ride && door(*change))
            {
                if (start_door(scene.shown->index, change->id == ui::control_id(ui::AttractionControl::Exit)))
                {
                    stop_land_tools(scene);
                    info->hide();
                }
            }
            else if (change && scene.shown)
            {
                change_attraction(device, scene, *info, *scene.shown, *change, assets, options);
                // Longer trains fit fewer times round the track, so the page shows the new limits; a new name and
                // the graph's series show.
                if (change->id == ui::control_id(ui::AttractionControl::Trains) || change->id == ui::control_id(ui::AttractionControl::Cars) || change->id == ui::control_id(ui::AttractionControl::Name) || change->id == ui::control_id(ui::AttractionControl::CarKind) || (change->id >= ui::control_id(ui::AttractionControl::SpeedSeries) && change->id <= ui::control_id(ui::AttractionControl::LateralSeries)))
                    show_info_page(scene, *info, assets, options);
            }
            else if (change && scene.shown_person)
            {
                if (change->id == ui::person_control(ui::PersonControl::Sack))
                {
                    fire(scene, *scene.shown_person);
                    scene.shown_person.reset();
                    info->hide();
                }
                else if (change_person(scene, *scene.shown_person, *change))
                {
                    show_info_page(scene, *info, assets, options);
                    if (change->id == ui::person_control(ui::PersonControl::Name))
                        name_cheats(scene, assets, *scene.shown_person, change->text);
                }
            }
            const auto paint = info->take_paint();
            if (paint && scene.shown_person)
                paint_person(scene, *scene.shown_person, paint->pot, paint->colour);
            if (paint && scene.shown_prop)
            {
                // Pot k paints the k-th slot the prop takes; free, as in the game.
                const std::vector<std::size_t> slots = ui::scenery_paint_slots(prop_facts(scene, *scene.shown_prop));
                if (paint->pot < slots.size())
                    paint_prop(device, scene, *scene.shown_prop, slots[paint->pot], static_cast<std::int32_t>(paint->colour));
            }
            if (const auto tab = info->take_picked(); tab && scene.shown_prop)
            {
                if (*tab < scene.prop_tabs.size())
                {
                    scene.prop_tab = scene.prop_tabs[*tab];
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
            if (const auto choice = info->take_choice(); choice && scene.shown)
            {
                // Put in the state picked, then shown again for the state tab's new look.
                const ui::AttractionFacts f = facts(scene, *scene.shown);
                const ui::AttractionKind kind = f.stall ? ui::AttractionKind::Stall : ui::attraction_kind(*f.ride);
                const std::vector<ui::StateChoice> states = ui::state_choices(kind);
                if (save::AttractionCommon* common = attraction_common(scene, *scene.shown); common != nullptr && choice->second < states.size())
                {
                    const save::AttractionState state = states[choice->second].state;
                    scene.refusal.clear();
                    if (f.ride != nullptr)
                        scene.refusal = change_ride_state(scene, scene.shown->index, state).value_or("");
                    else
                        set_attraction_state(scene, *common, state);
                    open_info(scene, *info, *scene.shown, assets, options);
                }
            }
            if (info->take_closed())
            {
                scene.shown.reset();
                scene.shown_person.reset();
                scene.shown_prop.reset();
            }
        }
        if (const auto asked = std::exchange(scene.rename_requested, std::nullopt); asked && change_person(scene, asked->first, ui::InfoChange { .id = ui::person_control(ui::PersonControl::Name), .text = asked->second }))
            name_cheats(scene, assets, asked->first, asked->second);
        if (const auto asked = std::exchange(scene.info_requested, std::nullopt); asked && info)
            open_info(scene, *info, *asked, assets, options);
        // The Construction tab has no page: it opens the panel that builds the ride's track.
        if (const auto asked = std::exchange(scene.construct_requested, std::nullopt); asked && asked->kind == park::AttractionRef::Kind::Ride)
        {
            // An open ride's track stays as it is; testing or closed, it may be changed.
            if (asked->index < scene.rides.rides.size() && scene.rides.rides[asked->index].base.state == save::AttractionState::Open)
            {
                scene.refusal = "CannotEditTrackedRideWhileOpen";
                if (info && scene.shown)
                    open_info(scene, *info, *scene.shown, assets, options);
            }
            else if (const auto overlay = ride_track_overlay(scene, asked->index))
            {
                if (open_track_building(scene, asked->index, *overlay, assets))
                {
                    close_footpath_panel(scene); // One tool panel at the bottom left at a time
                    stop_land_tools(scene);
                }
            }
        }
        profiling::section("TrackBuilding", [&] {
            const ui::Rect screen = desktop.root().rect();
            update_track_building(device, scene, assets, options.choice(config::GameOption::Currency), info_units(assets, options).distance, over ? std::nullopt : ground_tile(scene, pointer.position, { screen.width(), screen.height() }), input.commands, elapsed);
        });
        scene.view_camera.yaw += kViewTurn * elapsed;
        if (scene.shown_person && *scene.shown_person < scene.shown_people.size())
            scene.view_camera = person_view(scene, *scene.shown_person, scene.view_camera.yaw);
        const glm::vec3 viewer = park::eye(scene.camera, scene.seat);
        profiling::section("Objects", [&] { refresh_objects(device, scene, viewer, elapsed); });
        if (scene.billboards_eye != viewer)
        {
            const profiling::Scope zone("Billboards");
            for (ShownObjects* shown : { &scene.steady_objects, &scene.finer_objects, &scene.coarser_objects })
                park::face_billboards(shown->billboards, viewer);
            scene.billboards_eye = viewer;
        }
        scene.clock += elapsed;
        advance_park_particles(scene.particles, elapsed, viewer, static_cast<std::size_t>(options.get(config::GameOption::ParticleBudget)));
        scene.water.look = water_look(options);
        scene.shadows = options.choice(config::GameOption::Shadows);
        scene.blend_shadows = options.flag(config::GameOption::BlendShadows);
        scene.blend_band = options.flag(config::GameOption::BlendDetailLevels) ? options.get(config::GameOption::DetailBlendBand) : 0.0f;
        scene.bloom = options.flag(config::GameOption::Bloom);
        scene.light_shafts = options.flag(config::GameOption::LightShafts);
        scene.ambient_occlusion = options.flag(config::GameOption::AmbientOcclusion);
        scene.surface_detail = options.choice(config::GameOption::SurfaceDetail);
        advance_park_water(scene.water, elapsed);
        profiling::section("People", [&] { refresh_people(scene, viewer, park::view_projection(scene.camera, aspect(device), scene.seat), elapsed); });
        auto bar = desktop.root().find_as<ui::ParkBar>(kBarName);
        if (bar)
        {
            if (const auto asked = bar->take_speed())
                scene.park.speed = *asked;
        }
        for (const ui::PlayCommand c : input.commands)
        {
            if (c == ui::PlayCommand::TogglePause)
                scene.park.speed = simulation::pause_toggled(scene.park.speed);
            // The ride camera key leaves, or rides along on the ride whose window is open.
            else if (c == ui::PlayCommand::ToggleRideCamera && scene.riding_along())
                stop_riding(scene);
            else if (c == ui::PlayCommand::ToggleRideCamera && scene.shown && scene.shown->kind == park::AttractionRef::Kind::Ride)
                ride_cam(scene, assets, scene.shown->index);
            else if (c == ui::PlayCommand::NextRideCamera)
                ride_action(scene, assets, ui::RideCameraAction::NextCamera);
            else if (c == ui::PlayCommand::PreviousRideCamera)
                ride_action(scene, assets, ui::RideCameraAction::PreviousCamera);
            else if (c == ui::PlayCommand::NextRide)
                ride_action(scene, assets, ui::RideCameraAction::NextTrain);
            else if (c == ui::PlayCommand::PreviousRide)
                ride_action(scene, assets, ui::RideCameraAction::PreviousTrain);
            else if (c == ui::PlayCommand::NextCoasterCamType)
                ride_action(scene, assets, ui::RideCameraAction::ChangeType);
        }
        profiling::section("Simulation", [&] {
            // The park's day, its calendar and its messages' ages run at the speed picked, and stop while paused.
            const float game_seconds = elapsed * simulation::time_factor(scene.park.speed);
            simulation::advance(scene.day, game_seconds);
            // Each new week rates the park again; weeks run through in one frame are rated once.
            if (simulation::pass_time(scene.park, game_seconds) > 0)
            {
                // While the park runs, its own litter counts, and much of it is told to the player.
                const std::optional<std::int32_t> old = scene.simulate ? std::optional(simulation::old_litter(scene.sim)) : std::nullopt;
                profiling::section("Rating", [&] { simulation::rate_week(scene.park, scene.people, scene.rides, old); });
                if (old >= kLitterWarning)
                    simulation::post(scene.park.messages, { .text_key = "Thought_PI_Park_Litter1" });
                // The scenario's goals, checked as each week begins.
                std::int64_t rides_taken = 0;
                std::int64_t shops_taken = 0;
                for (const auto [e, attraction] : scene.sim.registry.view<const simulation::Attraction>().each())
                    (attraction.kind == simulation::AttractionKind::Ride ? rides_taken : shops_taken) += attraction.income.value;
                simulation::count_takings(scene.takings, scene.park.time, rides_taken, shops_taken);
                std::vector<simulation::GoalRide> goal_rides;
                for (std::size_t i = 0; i < scene.ride_groups.size() && i < scene.rides.rides.size(); ++i)
                    goal_rides.push_back({ scene.ride_groups[i], &scene.rides.rides[i] });
                // shortcut: the park's value stays as saved, as the simulation does not value the park yet; work it out once it does.
                const simulation::ParkFigures figures {
                    .park_value = std::llround(scene.park_model.finance.park_value.latest().value_or(0)),
                    .guests = scene.park.guests,
                    .rating = static_cast<std::int32_t>(std::lround(scene.park.rating.value_or(0) * simulation::kMaxRating)),
                    .loan = scene.park.loan,
                    .ride_income = scene.takings.rides,
                    .shop_profit = scene.takings.shops,
                    .rides = goal_rides,
                };
                if (const auto won = simulation::check_goals(scene.park_model.objectives, figures, scene.park.time))
                {
                    for (const std::int32_t prize : *won)
                    {
                        if (prize == 0)
                            continue;
                        simulation::give_prize(scene.park, scene.park_model.finance, scene.grid, prize);
                        simulation::post(scene.park.messages, { .text_key = std::string(simulation::prize_message(prize)) });
                        // A level won, every guest out on the paths jumps for joy.
                        if (prize >= simulation::kApprenticePrize)
                        {
                            for (const entt::entity guest : scene.sim.registry.view<simulation::Stride, simulation::Mood>())
                                scene.sim.registry.emplace_or_replace<simulation::Gesture>(guest, simulation::GestureKind::Cheer, simulation::kTuning.gestures.shown);
                        }
                    }
                }
                // Awards, as each month begins, from the park as it runs. The month the park opens in is not judged.
                const bool new_month = scene.awards_month >= 0 && scene.park.date.month != scene.awards_month;
                scene.awards_month = scene.park.date.month;
                if (new_month && scene.simulate && scene.park_model.has_awards)
                {
                    const entt::registry& r = scene.sim.registry;
                    const save::Finance& money = scene.park_model.finance;
                    simulation::AwardFigures f {
                        .guests = scene.park.guests,
                        .rating = static_cast<std::int32_t>(std::lround(scene.park.rating.value_or(0) * simulation::kMaxRating)),
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
                        const auto stall = std::ranges::find(scene.rides.stalls, attraction.saved, [](const save::Stall& s) { return s.base.id; });
                        if (stall != scene.rides.stalls.end() && !std::ranges::contains(foods, std::string_view(stall->base.symbol)))
                            foods.push_back(stall->base.symbol);
                    }
                    f.food_kinds = static_cast<std::int32_t>(foods.size());
                    for (std::size_t i = 0; i < scene.rides.rides.size(); ++i)
                    {
                        const save::Assessment& a = scene.rides.rides[i].assessment;
                        f.thrilling += a.valid && a.excitement > 5 && a.intensity > 5 ? 1 : 0;
                        const simulation::RideGroup group = i < scene.ride_groups.size() ? scene.ride_groups[i] : simulation::RideGroup::Other;
                        f.coasters += group == simulation::RideGroup::Coaster ? 1 : 0;
                        f.water_rides += group == simulation::RideGroup::Water ? 1 : 0;
                    }
                    for (const auto [e, upkeep] : r.view<const simulation::Upkeep>().each())
                    {
                        f.downtime.push_back(upkeep.downtime_share() * 100);
                        f.reliability += upkeep.reliability.value * 100;
                        const auto* attraction = r.try_get<const simulation::Attraction>(e);
                        f.running += attraction != nullptr && attraction->open && !attraction->testing && !upkeep.broken ? 1 : 0;
                    }
                    // shortcut: rides do not crash here, so only crashes the save remembers count; count new ones once
                    // trains can crash.
                    f.crashed = static_cast<std::int32_t>(std::ranges::count_if(scene.rides.rides, [](const save::Ride& ride) { return ride.base.time_since_crash >= 0; }));
                    f.coaster_dangers = scene.park_model.awards.dangerous_coaster_events.value_or(0);
                    f.pool_dangers = scene.park_model.awards.dangerous_pool_events.value_or(0);
                    if (scene.build.items.valid() && scene.build.items.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                        f.themed = simulation::themed_attractions(scene.rides, scene.objects, scene.build.items.get());
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
                        if (scene.park.cash && slot < scene.park_model.awards.prize_money.size())
                            *scene.park.cash += scene.park_model.awards.prize_money[slot];
                        simulation::post(scene.park.messages, { .text_key = std::string(simulation::award_message(kind)), .award = kind });
                    }
                }
            }
            simulation::age(scene.park.messages, game_seconds);
            if (scene.simulate)
            {
                // A long frame steps at most a few seconds, so the park catches up rather than stalls.
                constexpr float kMostSeconds = 2;
                scene.sim_time = std::min(scene.sim_time + game_seconds, kMostSeconds);
                scene.sim.registry.ctx().insert_or_assign(simulation::guest_weather(scene.park));
                scene.sim.registry.ctx().insert_or_assign(simulation::Daylight { scene.day.time });
                const std::int64_t fee = scene.park_model.has_finance ? scene.park_model.finance.entrance_fee : 0;
                scene.sim.registry.ctx().insert_or_assign(simulation::Draw { .entrance_fee = { fee }, .pool_squares = static_cast<std::uint32_t>(scene.footpaths.pools.size()), .rating = scene.park.rating });
                profiling::section("Guests", [&] {
                    for (; scene.sim_time >= simulation::kTickSeconds; scene.sim_time -= simulation::kTickSeconds)
                        simulation::step(scene.sim);
                });
                // The guests in the park now, coming in at the gate and going home, for the bar and the rating.
                const auto inside = scene.sim.registry.view<const simulation::Visitor>(entt::exclude<simulation::Left>);
                scene.park.guests = static_cast<std::int32_t>(std::ranges::distance(inside.begin(), inside.end()));
                // Each thing broken costs the park some rating, and bad runs of it are told to the player.
                if (const auto* vandalism = scene.sim.registry.ctx().find<simulation::Vandalism>())
                {
                    const auto broken = static_cast<float>(vandalism->broken - scene.vandalism_seen);
                    if (scene.park.rating)
                        *scene.park.rating = std::max(0.0f, *scene.park.rating - kVandalismPenalty * broken / static_cast<float>(simulation::kMaxRating));
                    scene.vandalism_seen = vandalism->broken;
                    for (; scene.vandalism_warnings_seen < vandalism->warnings; ++scene.vandalism_warnings_seen)
                        simulation::post(scene.park.messages, { .text_key = "Thought_PI_Park_Vandalism1" });
                }
                // What is broken looks it, and looks whole again once mended.
                park::Broken broken;
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
                    const bool wanted = options.flag(config::GameOption::AllPhotos) && options.flag(config::GameOption::PhotoSpotPhotos);
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
                        scene.photos.push_back({ park::framed(points, yaw, kPhotoPitch, kPhotoClosest), std::format("{} at photo spot_", name.empty() ? "Guest" : name) });
                    }
                }
                // New thoughts go into the guests' saved thoughts, newest first, for their windows to show.
                for (ShownPerson& p : scene.shown_people)
                {
                    const auto* thoughts = p.staff || p.guest == entt::null || p.saved >= scene.people.guests.size() ? nullptr : scene.sim.registry.try_get<simulation::Thoughts>(p.guest);
                    if (thoughts == nullptr || thoughts->had == p.thoughts_seen)
                        continue;
                    auto& slots = scene.people.guests[p.saved].person.thoughts;
                    for (std::size_t k = std::min<std::size_t>(thoughts->had - p.thoughts_seen, thoughts->recent.size()); k-- > 0;)
                    {
                        const simulation::Thought& t = thoughts->recent[k];
                        const float newest = std::ranges::max(slots, { }, &save::Thought::time).time;
                        const auto* about = scene.sim.registry.valid(t.about) ? scene.sim.registry.try_get<simulation::Attraction>(t.about) : nullptr;
                        std::shift_right(slots.begin(), slots.end(), 1);
                        slots[0] = { .about = { about != nullptr ? about->saved : 0 }, .id = std::to_underlying(t.kind), .time = newest + 1, .variant = static_cast<std::int32_t>(t.variant) };
                    }
                    p.thoughts_seen = thoughts->had;
                }
                // Staff are paid from the park's cash, and the player hears of rides breaking down and being fixed.
                if (const auto* payroll = scene.sim.registry.ctx().find<simulation::Payroll>())
                {
                    if (scene.park.cash)
                        *scene.park.cash -= payroll->paid.value - scene.wages_seen;
                    scene.wages_seen = payroll->paid.value;
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
                    save::Ride& ride = scene.rides.rides[i];
                    ride.breakdown.reliability = upkeep->reliability.value * 100;
                    ride.breakdown.current = static_cast<save::BreakdownKind>(std::to_underlying(upkeep->kind));
                    ride.breakdown.last = static_cast<save::BreakdownKind>(std::to_underlying(upkeep->last));
                    ride.base.time_since_inspection = upkeep->since_inspection.value;
                    ride.breakdown.downtime_windows = upkeep->downtime;
                    ride.breakdown.saved_downtime_percent.reset();
                    using enum save::RepairStatus;
                    ride.base.repair_status = !upkeep->broken ? Fine : upkeep->mechanic == entt::null ? MechanicCalled : MechanicOnTheWay;
                    ride.base.flags = upkeep->broken ? ride.base.flags | save::AttractionCommon::kBrokenDown : ride.base.flags & ~save::AttractionCommon::kBrokenDown;
                }
                for (const auto [e, upkeep, attraction] : scene.sim.registry.view<const simulation::Upkeep, const simulation::Attraction>().each())
                {
                    const auto told = std::ranges::find(scene.broken_rides, e);
                    if (upkeep.broken == (told != scene.broken_rides.end()))
                        continue;
                    if (upkeep.broken)
                        scene.broken_rides.push_back(e);
                    else
                        scene.broken_rides.erase(told);
                    const auto at = std::ranges::find(scene.ride_entities, e);
                    const auto index = static_cast<std::size_t>(at - scene.ride_entities.begin());
                    simulation::post(scene.park.messages, { .text_key = upkeep.broken ? "RideBrokenMessage" : "RideFixedMessage", .subject = index < scene.rides.rides.size() ? std::optional(scene.rides.rides[index].base.id) : std::nullopt, .subject_name = attraction.name, .panel = ui::message_panel(ui::AttractionTab::Maintenance) });
                    // A broken ride is taped off and gets its own fence back once fixed: the tape panels stand where
                    // the fence's do, so each panel only changes its model.
                    if (index >= scene.rides.rides.size())
                        continue;
                    save::Ride& ride = scene.rides.rides[index];
                    const park::ObjectModel* item = nullptr;
                    for (const save::PlacedObject& o : scene.objects)
                    {
                        if (ride.base.spot && o.id == ride.base.spot->object)
                        {
                            const auto found = scene.models.find(park::model_ref(o));
                            item = found != scene.models.end() ? &found->second : nullptr;
                        }
                    }
                    const std::vector<save::PlacedObject> before = park::ride_fences(ride, item, 0);
                    ride.override_fence_symbol = upkeep.broken ? kTapeFence : "";
                    ride.override_fence_overlay = upkeep.broken ? kTapeFenceOverlay : "";
                    // shortcut: a ride with no fence of its own stays unfenced while broken; add panels for it if
                    // such rides turn up outside old saves.
                    const std::vector<save::PlacedObject> after = park::ride_fences(ride, item, 0);
                    if (before.empty() || (!after.empty() && (before.size() != after.size() || !load_object_model(scene, scene.placing.models, assets, after.front()))))
                        continue;
                    wait_for_build(scene);
                    for (std::size_t i = 0; i < before.size(); ++i)
                    {
                        const save::PlacedObject& was = before[i];
                        const auto panel = std::ranges::find_if(scene.objects, [&](const save::PlacedObject& o) { return !o.hidden && o.symbol == was.symbol && o.overlay == was.overlay && o.col == was.col && o.row == was.row && o.rotation == was.rotation && o.base_height == was.base_height; });
                        if (panel == scene.objects.end())
                            continue;
                        panel->hidden = after.empty(); // Fixed, with no fence of its own
                        if (after.empty())
                            continue;
                        panel->symbol = after[i].symbol;
                        panel->overlay = after[i].overlay;
                    }
                    objects_changed(scene);
                }
            }
            profiling::section("Cars", [&] { place_cars(scene, viewer, park::view_projection(scene.camera, aspect(device), scene.seat), elapsed); });
        });
        profiling::section("Daylight", [&] { show_time_of_day(scene, viewer); });

        const profiling::Scope zone("Bar");
        if (bar)
        {
            ui::ParkBarUnits units;
            units.currency = ui::currency(*assets.strings, options.choice(config::GameOption::Currency));
            units.fahrenheit = options.choice(config::GameOption::TemperatureUnits) == kFahrenheit;
            // The game writes its times with am and pm in English only.
            units.twelve_hour = assets.fonts.language == ui::kDefaultLanguage;
            bar->show(ui::park_bar_values(scene.park, scene.day.time, *assets.strings, units));
        }
        if (auto messages = desktop.root().find_as<ui::MessageDrawer>(kMessagesName))
        {
            // shortcut: only rides and stalls can be gone to, not people or path items; add them when a message names one.
            // Saved parks keep messages without their subject's name, so it is filled in from the attraction.
            for (simulation::ParkMessage& m : scene.park.messages.list)
            {
                const auto ref = m.subject && m.subject_name.empty() ? attraction_with(scene, *m.subject) : std::nullopt;
                if (ref)
                    m.subject_name = strings::to_utf8(ui::attraction_name(ref->kind == park::AttractionRef::Kind::Stall ? scene.rides.stalls[ref->index].base : scene.rides.rides[ref->index].base, *assets.strings));
            }
            std::vector<ui::DrawerMessage> shown = ui::drawer_messages(scene.park.messages, *assets.strings);
            for (std::size_t i = 0; i < shown.size(); ++i)
            {
                const auto& subject = scene.park.messages.list[i].subject;
                shown[i].located = subject && attraction_with(scene, *subject);
            }
            messages->show(shown);
            scene.message_alert = scene.message_alert || messages->take_alert();
            // A message's camera takes the view to its subject and opens the info panel on it, as the game does.
            const auto serial = messages->take_located();
            const auto message = serial ? std::ranges::find(scene.park.messages.list, *serial, &simulation::ParkMessage::serial) : scene.park.messages.list.end();
            if (const auto ref = message != scene.park.messages.list.end() && message->subject ? attraction_with(scene, *message->subject) : std::nullopt; ref && info)
            {
                // At the tab the message names; one the attraction lacks opens its view.
                open_info(scene, *info, *ref, assets, options, ui::message_tab(message->panel.value_or(-1)));
                // The middle of the whole attraction, the same on every click, as in the game.
                const glm::vec3 at = scene.view_camera.target;
                // Close enough to make it out, never further out than the player was.
                scene.camera.target = { at.x, scene.camera.target.y, at.z };
                scene.camera.distance = std::min(scene.camera.distance, std::max(scene.view_camera.distance * kFocusZoomOut, kFocusClosest));
                scene.camera = park::on_ground(scene.camera, scene.grid);
                // It shows in the middle of what the drawer leaves of the screen, not behind the drawer: the view
                // slides on along the ground until it does, found by the secant method from no slide.
                const ui::Rect screen = desktop.root().rect();
                const float want = (messages->rect().y1 + screen.y1) / 2;
                const auto off = [&](float slide) {
                    const glm::vec4 clip = park::view_projection(park::panned(scene.camera, 0, slide), screen.width() / screen.height()) * glm::vec4(at, 1.0f);
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
                scene.camera = park::on_ground(park::panned(scene.camera, 0, slide), scene.grid);
            }
        }
        if (auto tools = desktop.root().find_as<ui::ToolRow>(kToolsName))
        {
            const std::optional<ui::ParkTool> pressed = tools->take_pressed();
            if (pressed == ui::ParkTool::Leave)
                ask_leave(scene, *assets.strings, assets.palette);
            else if (pressed == ui::ParkTool::Map)
                toggle_park_map(scene.map, desktop, *assets.strings, assets.palette);
            else if (pressed == ui::ParkTool::Paths)
            {
                close_track_building(scene); // One tool panel at the bottom left at a time
                stop_ride_placing(scene);
                stop_land_tools(scene);
                toggle_footpath_panel(device, scene, assets, options);
            }
            if (const auto item = tools->take_item())
            {
                if (*item == ui::ToolItem::ShapeGround || *item == ui::ToolItem::PaintGround || *item == ui::ToolItem::Water)
                {
                    close_track_building(scene);
                    stop_ride_placing(scene);
                    close_footpath_panel(scene);
                    toggle_land_tool(scene, *item == ui::ToolItem::ShapeGround ? LandTools::Tool::Shape : (*item == ui::ToolItem::PaintGround ? LandTools::Tool::Paint : LandTools::Tool::Water), assets);
                }
                else if (*item == ui::ToolItem::People)
                    scene.show_people = tools->switched_on(*item);
                else if (*item == ui::ToolItem::Scenery)
                    scene.show_scenery = tools->switched_on(*item);
                else if (*item == ui::ToolItem::SeeThroughGround)
                    scene.see_through_ground = !tools->switched_on(*item); // Switched on, the ground shows whole
                else if (*item == ui::ToolItem::Grid)
                    scene.show_grid = tools->switched_on(*item);
                else if (*item == ui::ToolItem::Contours)
                    scene.show_contours = tools->switched_on(*item);
                else if (*item == ui::ToolItem::PathHeights)
                    scene.show_path_heights = tools->switched_on(*item);
                else if (*item == ui::ToolItem::RideHeights)
                    scene.show_ride_heights = tools->switched_on(*item);
                else if (*item == ui::ToolItem::Money)
                    scene.show_money = tools->switched_on(*item);
                else if (*item == ui::ToolItem::GuestPayments)
                    scene.show_guest_payments = tools->switched_on(*item);
                else if (*item == ui::ToolItem::Supports)
                {
                    scene.show_supports = tools->switched_on(*item);
                    scene.levels = { };
                    scene.since_rebuild = kObjectRebuildInterval;
                }
                else if (*item == ui::ToolItem::Options || *item == ui::ToolItem::About || *item == ui::ToolItem::Save || *item == ui::ToolItem::Load)
                    scene.window_requests.push_back(*item);
                else if (const auto topic = ui::management_topic(*item))
                    toggle_management_window(desktop, *topic, management_facts(scene), *assets.strings, assets.palette, info_units(assets, options));
                else
                    open_build_list(scene.build, desktop.root(), *item, tools->item(*item), *tools, *assets.strings, assets.palette, ui::currency(*assets.strings, options.choice(config::GameOption::Currency)));
            }
            // A ride's window names it after its kind, looked up by the kind's symbol; some kinds' files give their
            // name under another key, so once the items are read each such kind's name is kept under its symbol too.
            if (!scene.build.kinds_named && scene.build.items.valid() && scene.build.items.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                scene.build.kinds_named = true;
                for (const park::BuildItem& item : scene.build.items.get())
                {
                    const std::string own = std::format("{}Name", item.symbol);
                    if (const auto name = assets.strings->get(item.name_text); !name.empty() && assets.strings->get_ignoring_case(own).empty())
                        assets.strings->add(own, std::u16string(name));
                }
                // A ride's window opened before then named it from the kind's name it could not yet find.
                if (info && info->visible)
                    show_info_page(scene, *info, assets, options);
            }
            if (const auto picked = update_build_menus(scene.build, desktop.root(), scene.park.cash))
            {
                bool building = false;
                if (start_ride_placing(scene, *picked))
                {
                    close_track_building(scene);
                    close_footpath_panel(scene);
                    stop_land_tools(scene);
                    building = picked->kind != park::BuildKind::Scenery;
                }
                else if (new_tracked_ride(scene, *picked, assets))
                {
                    stop_ride_placing(scene);
                    close_footpath_panel(scene);
                    stop_land_tools(scene);
                    building = true;
                }
                // Building a ride puts the build menus away, as in the game; scenery keeps its list out for the
                // next piece.
                if (building)
                {
                    close_build_list(desktop.root());
                    if (auto tools = desktop.root().find_as<ui::ToolRow>(kToolsName); tools && tools->open_menu() && (*tools->open_menu() == ui::ParkTool::Rides || *tools->open_menu() == ui::ParkTool::Shops || *tools->open_menu() == ui::ParkTool::Scenery || *tools->open_menu() == ui::ParkTool::Paths))
                        tools->close_menu();
                }
            }
        }
        update_view_lines(device, scene);
        if (auto heights = desktop.root().find_as<ui::WorldLabels>(kHeightsName))
        {
            const ui::Rect view = desktop.root().rect();
            heights->labels = height_labels(scene, { view.width(), view.height() });
        }
        if (auto money = desktop.root().find_as<ui::MoneyFloaters>(kMoneyName))
        {
            const ui::Rect view = desktop.root().rect();
            money->floats = money_floats(scene, { view.width(), view.height() }, ui::currency(*assets.strings, options.choice(config::GameOption::Currency)), elapsed);
        }
        if (const auto picked = update_park_map(device, scene.map, desktop, scene.grid, scene.camera, aspect(device), [&] { return map_people(scene); }, elapsed))
        {
            scene.camera.target = { picked->x, scene.camera.target.y, picked->y };
            scene.camera = park::on_ground(scene.camera, scene.grid);
        }
        const ui::ManagementFacts facts = management_facts(scene);
        const ManagementAsks asks = update_management_windows(desktop, facts, *assets.strings, info_units(assets, options));
        if (asks.row && info)
            open_row(scene, *info, *asks.row, assets, options);
        // Every broken path item back new at once, at the full price of each: the dear way, a handyman's the free one.
        if (asks.replace_broken && facts.broken_items > 0 && (!scene.park.cash || *scene.park.cash >= facts.replace_cost))
        {
            simulation::replace_broken(scene.sim);
            if (scene.park.cash)
                *scene.park.cash -= facts.replace_cost;
        }
        profiling::section("Footpaths", [&] { update_footpaths(device, scene, assets, options); });
        follow_footpaths(scene, elapsed);
        if (auto guide = desktop.root().find_as<ui::TutorialGuide>(kGuideName); guide && guide->take_quit())
            ask_leave(scene, *assets.strings, assets.palette);
        if (auto question = desktop.root().find_as<ui::QuestionBox>(kQuestionName))
        {
            if (const auto answer = question->take_answer())
                scene.leave = scene.leave || *answer == 0;
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
        if (auto paths = scene.desktop->root().find_as<const ui::FootpathPanel>(kFootpathPanelName); paths && paths->showing() && paths->page() == ui::FootpathPage::Controls)
            pointer.tool = ui::PointerTool::Path;
        return ui::park_cursor(pointer);
    }

    std::vector<std::byte> save_park(const ParkScene& scene)
    {
        parkfile::SavedPark saved;
        saved.base = scene.base;
        saved.objects = parkfile::kept_objects(scene.objects, scene.own_objects, scene.footpaths.paths, scene.rides);
        if (auto summary = save::read_summary(scene.base))
            saved.summary = std::move(*summary);
        // shortcut: the picture stays the original park's and the day is the week's first; draw the park and
        // count days when the calendar does.
        saved.summary.year = static_cast<std::int32_t>(scene.park.date.year) + 1;
        saved.summary.month = scene.park.date.month;
        saved.summary.day = scene.park.date.week * 7 + 1;
        saved.summary.minutes = static_cast<std::int32_t>(scene.day.time * 24 * 60);
        saved.summary.guests = scene.park.guests;
        saved.grid = scene.grid;
        saved.paths = scene.footpaths.paths;
        saved.bins = scene.bins;
        saved.signs = scene.signs;
        saved.rides = scene.rides;
        const simulation::ParkState& park = scene.park;
        saved.progress = { .cash = park.cash, .loan = park.loan, .rating = park.rating, .rating_parts = park.rating_parts, .time = park.time, .zero_time = park.zero_time, .date = park.date, .weather = park.weather, .forecast = park.forecast, .temperature = park.temperature, .day_time = scene.day.time };
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
        for (const auto [e, item] : scene.sim.registry.view<const simulation::PathItem>().each())
            if (item.broken && item.object != 0)
                saved.broken_items.push_back(item.object);
        if (!scene.park_model.objectives.objectives.empty())
            saved.goals = parkfile::SavedGoals { .objectives = scene.park_model.objectives, .max_loan = scene.park_model.finance.max_loan, .interest_rate_percent = scene.park_model.finance.interest_rate_percent };
        return parkfile::write_saved_park(saved);
    }

} // namespace rct3::scene
