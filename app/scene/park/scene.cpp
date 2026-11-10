// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park/scene.hpp"

#include "career/career_file.hpp"
#include "config/game_saves.hpp"
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
#include "scene/park/holding.hpp"
#include "scene/park/management.hpp"
#include "scene/park/map_view.hpp"
#include "scene/park/patrol.hpp"
#include "scene/park/questions.hpp"
#include "scene/park/refresh.hpp"
#include "scene/park/ride_camera.hpp"
#include "scene/park/setup.hpp"
#include "scene/park/view.hpp"
#include "scene/park/water.hpp"
#include "scene/pool_tools.hpp"
#include "scene/track_building.hpp"
#include "simulation/animals.hpp"
#include "simulation/awards.hpp"
#include "simulation/components.hpp"
#include "simulation/guests.hpp"
#include "simulation/import_park.hpp"
#include "simulation/parties.hpp"
#include "simulation/rides.hpp"
#include "simulation/staff_care.hpp"
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
#include "ui/screens/attraction_info.hpp"
#include "ui/screens/delete_area_panel.hpp"
#include "ui/screens/message_drawer.hpp"
#include "ui/screens/pool_panel.hpp"
#include "ui/screens/ride_camera_panel.hpp"
#include "ui/screens/tool_menus.hpp"
#include "ui/screens/tool_row.hpp"
#include "ui/screens/tutorial_guide.hpp"
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
        constexpr int kFahrenheit = 1;                            ///< The temperature units option's choice for Fahrenheit
        constexpr std::string_view kTapeFence = "KeepClearFence"; ///< Closes a broken ride off until it is fixed
        constexpr std::string_view kTapeFenceOverlay = "Style\\Vanilla\\style";
        constexpr std::string_view kOk = "SMB_Label_OK";
        constexpr std::string_view kPrizeName = "prize_won"; ///< The box telling a prize won, one at a time
        constexpr glm::vec4 kDeleteMarkColour { 1.0f, 0.35f, 0.3f, 0.9f };

        constexpr float kHoverPickInterval = 0.1f; ///< Seconds between looks at what is under the pointer, for its look

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

        /// The first of the attraction's objects that takes colours, and the slots it takes.
        std::optional<std::pair<std::size_t, std::vector<std::size_t>>> painted_object(const ParkScene& scene, rct3::park::AttractionRef ref)
        {
            for (std::size_t i = 0; i < scene.objects.size(); ++i)
            {
                const auto owner = scene.attractions.find(scene.objects[i].id);
                if (owner == scene.attractions.end() || owner->second != ref)
                    continue;
                const auto model = scene.models.find(rct3::park::object::model_ref(scene.objects[i]));
                ui::screens::SceneryFacts f;
                f.colours = scene.objects[i].colours;
                f.colour_slots = model == scene.models.end() ? 0 : rct3::park::object::colour_slots(model->second);
                if (std::vector<std::size_t> slots = ui::screens::scenery_paint_slots(f); !slots.empty())
                    return std::pair { i, std::move(slots) };
            }
            return std::nullopt;
        }

        /// The kind `stall` is of in the build lists, once they are read.
        const rct3::park::BuildItem* stall_kind(const ParkScene& scene, const save::model::Stall& stall)
        {
            const auto& items = scene.build.items;
            if (!items.valid() || items.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return nullptr;
            const auto kind = std::ranges::find_if(items.get(), [&](const rct3::park::BuildItem& i) { return i.stall && i.overlay == stall.base.overlay_path && std::format("{}:sta", i.symbol) == stall.base.symbol; });
            return kind == items.get().end() ? nullptr : &*kind;
        }

        ui::screens::AttractionFacts facts(const ParkScene& scene, rct3::park::AttractionRef ref)
        {
            ui::screens::AttractionFacts f { .guests = scene.people.guests, .refusal = scene.refusal };
            if (ref.kind == rct3::park::AttractionRef::Kind::Stall)
            {
                if (ref.index < scene.rides.stalls.size())
                {
                    f.stall = &scene.rides.stalls[ref.index];
                    f.common = &f.stall->base;
                    if (const rct3::park::BuildItem* kind = stall_kind(scene, *f.stall))
                        for (const rct3::park::BuildItem::Goods& g : kind->goods)
                            f.offers.push_back(std::format("{}:cid", g.item));
                    f.carried = scene.carried.items;
                    f.carried_extras = scene.carried.extras;
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
                const auto show = std::ranges::find(scene.shows, f.ride->base.id, &save::model::RideShow::ride);
                f.show = show == scene.shows.end() ? nullptr : &*show;
                // A flat ride's doors go round the ground it stands on.
                if (f.ride->kind == save::model::RideKind::Flat)
                    f.station = f.ride->base.spot.has_value();
                if (f.ride->kind == save::model::RideKind::Tracked)
                {
                    const auto& course = ref.index < scene.courses.size() ? scene.courses[ref.index] : std::nullopt;
                    f.station = !f.ride->stations.empty() || (course && std::ranges::contains(course->kinds, rct3::park::track::CourseKind::Station));
                    f.pool_ride = ref.index < scene.track_rules.size() && scene.track_rules[ref.index].pool_ride;
                    // Its trains can change while it is not open, as in the game, and while none of them has riders
                    // aboard (set_trains).
                    const entt::entity e = scene.simulate && ref.index < scene.ride_entities.size() ? scene.ride_entities[ref.index] : entt::null;
                    const auto* fleet = e != entt::null ? scene.sim.registry.try_get<simulation::Trains>(e) : nullptr;
                    const bool aboard = fleet && std::ranges::any_of(fleet->riders, [](const auto& riders) { return !riders.empty(); });
                    const auto limits = scene.train_limits.find(ref.index);
                    if (limits != scene.train_limits.end() && f.ride->base.state != save::model::AttractionState::Open && !aboard)
                    {
                        const auto choices = scene.train_choices.find(ref.index);
                        f.other_cars = choices != scene.train_choices.end() && choices->second.size() > 1;
                        f.trains = limits->second;
                        // A ride from a saved park measured by its cars' kept lengths until its trains are made here.
                        std::vector<rct3::park::object::CarFit> kept;
                        for (const save::model::Car& car : f.ride->trains.empty() ? std::vector<save::model::Car> { } : f.ride->trains.front().cars)
                            kept.push_back({ .length = car.length });
                        const auto fits = scene.car_fits.find(ref.index);
                        const rct3::park::track::TrackRules rules = ref.index < scene.track_rules.size() ? scene.track_rules[ref.index] : rct3::park::track::TrackRules { };
                        // A shuttle runs one (fit_trains).
                        const auto coaster = course ? simulation::coaster_of(*course, *f.ride) : std::nullopt;
                        f.trains->most_trains = course && !(coaster && coaster->shuttle != simulation::Shuttle::None) ? rct3::park::track::trains_fitting(*course, fits != scene.car_fits.end() ? std::span<const rct3::park::object::CarFit>(fits->second) : kept, f.ride->options.block_sectioned, rules.most_trains) : 1;
                    }
                }
            }
            // A running ride's seats, queue and riders an hour.
            if (f.ride != nullptr && scene.simulate && ref.index < scene.ride_entities.size() && scene.ride_entities[ref.index] != entt::null)
            {
                if (const auto* cycle = scene.sim.registry.try_get<simulation::CycleRide>(scene.ride_entities[ref.index]))
                {
                    f.flow = ui::screens::RideFlow { cycle->capacity, cycle->queue.size(), cycle->per_hour, cycle->riders.size() };
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

        /// What the saved worker's window shows of `staff`'s pay, training, mood and work done.
        void copy_mood(const simulation::Staff& staff, save::model::Staff& saved)
        {
            saved.wage_level = staff.wage_level;
            saved.training = staff.training;
            saved.warnings = staff.warnings;
            saved.happiness = staff.happiness.value;
            saved.laziness = staff.laziness.value;
            if (auto* w = std::get_if<save::model::HandymanWork>(&saved.work))
            {
                w->paths_swept = staff.swept;
                w->bins_emptied = staff.emptied;
                w->gardens_watered = staff.watered;
            }
            else if (auto* m = std::get_if<save::model::MechanicWork>(&saved.work))
            {
                m->objects_fixed = staff.fixed;
                m->objects_inspected = staff.inspected;
            }
            else if (auto* k = std::get_if<save::model::KeeperWork>(&saved.work))
            {
                k->dung_cleared = staff.cleared;
                k->troughs_filled = staff.filled;
            }
        }

        ui::screens::PersonFacts person_facts(const ParkScene& scene, std::size_t shown)
        {
            ui::screens::PersonFacts f;
            f.people = &scene.people;
            f.rides = &scene.rides;
            f.objects = scene.objects;
            if (shown >= scene.shown_people.size())
                return f;
            const ShownPerson& p = scene.shown_people[shown];
            f.zero_time = scene.park.zero_time;
            if (p.staff && p.saved < scene.people.staff.size())
            {
                f.staff = &scene.people.staff[p.saved];
                simulation::Staff pay { .role = simulation::staff_role(f.staff->kind), .training = f.staff->training };
                simulation::set_wage_level(pay, f.staff->wage_level);
                f.wage = pay.wage.value;
                f.training_cost = simulation::training_cost(pay);
            }
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
            const auto& r = scene.sim.registry;
            if (const ShownPerson& p = scene.shown_people[shown]; !p.staff && p.guest != entt::null && r.valid(p.guest) && r.all_of<simulation::Vip>(p.guest))
            {
                const auto& vip = r.get<simulation::Vip>(p.guest);
                const auto& goals = scene.park_model.objectives.conditions;
                const auto goal = std::ranges::find(goals, vip.goal, &save::model::Condition::id);
                const float happiness = r.all_of<simulation::Mood>(p.guest) ? r.get<simulation::Mood>(p.guest).happiness.value : 0.0f;
                if (goal != goals.end() && goal->vip)
                    f.vip = { vip.patience.value, simulation::vip_opinion(vip, *goal->vip, happiness) };
            }
            return f;
        }

        ui::screens::InfoUnits info_units(const ui::InterfaceAssets& assets, const config::GameOptions& options)
        {
            ui::screens::InfoUnits units;
            units.currency = ui::currency(*assets.strings, options.choice(config::GameOption::Currency));
            units.distance = static_cast<ui::screens::DistanceUnits>(options.choice(config::GameOption::DistanceUnits));
            return units;
        }

        /// The changing room (of the pools') the object `o` draws, if it draws one.
        std::optional<std::size_t> pool_room(const ParkScene& scene, const save::model::PlacedObject& o)
        {
            if (!o.fixture || !std::ranges::contains(scene.pool_pieces, o.id))
                return std::nullopt;
            const auto room = std::ranges::find_if(scene.pools.rooms, [&](const rct3::park::pools::PoolRoom& r) { return r.col == o.col && r.row == o.row && r.symbol == o.symbol; });
            return room == scene.pools.rooms.end() ? std::nullopt : std::optional(static_cast<std::size_t>(room - scene.pools.rooms.begin()));
        }

        /// The simulated changing room of `room`, if the simulation has it.
        entt::entity room_entity(const ParkScene& scene, std::size_t room)
        {
            for (const auto [e, door] : scene.sim.registry.view<const simulation::PoolDoor>().each())
                if (door.col == scene.pools.rooms[room].col && door.row == scene.pools.rooms[room].row)
                    return e;
            return entt::null;
        }

        ui::screens::SceneryFacts prop_facts(const ParkScene& scene, std::size_t index)
        {
            ui::screens::SceneryFacts f;
            if (index >= scene.objects.size())
                return f;
            const save::model::PlacedObject& o = scene.objects[index];
            f.symbol = o.symbol;
            f.colours = o.colours;
            f.zero_time = scene.park.zero_time;
            if (const auto model = scene.models.find(rct3::park::object::model_ref(o)); model != scene.models.end())
                f.colour_slots = rct3::park::object::colour_slots(model->second);
            if (const auto bin = std::ranges::find(scene.bins, o.id, &save::model::BinState::object_ref); bin != scene.bins.end())
                f.bin = &*bin;
            if (const auto sign = std::ranges::find(scene.signs, o.id, &save::model::SignState::object_ref); sign != scene.signs.end())
                f.sign = &*sign;
            if (const auto room = pool_room(scene, o))
            {
                // Its complex's swimmers, and its water's dirt, as the simulation has them.
                const auto& r = scene.sim.registry;
                const entt::entity e = room_entity(scene, *room);
                const auto* door = e != entt::null ? &r.get<simulation::PoolDoor>(e) : nullptr;
                const auto* pool = door != nullptr && r.valid(door->pool) ? r.try_get<simulation::SwimPool>(door->pool) : nullptr;
                std::int64_t people = 0;
                for (const auto [g, swimmer, errand] : r.view<const simulation::Swimmer, const simulation::Errand>().each())
                    people += door != nullptr && r.valid(errand.target) && r.all_of<simulation::PoolDoor>(errand.target) && r.get<simulation::PoolDoor>(errand.target).pool == door->pool;
                const rct3::park::pools::PoolRoom& at = scene.pools.rooms[*room];
                f.pool = ui::screens::PoolComplexFacts {
                    .name = strings::to_utf16(at.complex_name), .number = static_cast<std::size_t>(std::ranges::count(scene.pools.rooms.begin(), scene.pools.rooms.begin() + static_cast<std::ptrdiff_t>(*room) + 1, at.symbol, &rct3::park::pools::PoolRoom::symbol)), .ticket = at.ticket, .open = at.open, .people = people, .dirt = pool != nullptr ? simulation::dirt_step(*pool) : 0, .fluid = at.fluid, .waves = at.waves, .refill = simulation::kTuning.pools.refill.at(std::min<std::size_t>(at.fluid, 2)).value
                };
                // The room's takings and upkeep, and what guests thought of it, as the simulation has them.
                if (const auto* attraction = e != entt::null ? r.try_get<simulation::Attraction>(e) : nullptr)
                {
                    f.pool->customers = attraction->customers;
                    f.pool->income = attraction->income.value;
                }
                if (const auto* upkeep = e != entt::null ? r.try_get<simulation::Upkeep>(e) : nullptr)
                {
                    f.pool->reliability = upkeep->reliability.value;
                    f.pool->since_inspection = upkeep->since_inspection.value;
                    if (const auto interval = std::ranges::find(simulation::kInspectionSeconds, upkeep->inspect_every.value); interval != simulation::kInspectionSeconds.end())
                        f.pool->inspection = static_cast<int>(interval - simulation::kInspectionSeconds.begin());
                }
                for (const auto [g, thoughts] : r.view<const simulation::Thoughts>().each())
                    for (const simulation::Thought& t : thoughts.recent)
                        if (e != entt::null && t.about == e)
                            f.pool->thoughts.push_back({ .thought = static_cast<std::int32_t>(std::to_underlying(t.kind)), .time = t.time.value, .variant = t.variant });
                const std::vector<save::model::Tile> complex = rct3::park::pools::pool_complex(scene.pools, *room);
                const auto machine = [&](const rct3::park::pools::PoolExtra& x) { return x.fit.wave_machine && std::ranges::contains(complex, save::model::Tile { x.col, x.row }); };
                f.pool->machines = std::ranges::any_of(scene.pools.extras, machine) || std::ranges::any_of(scene.scenery_pool_extras, machine);
            }
            return f;
        }

        /// Adds the pictures of what stalls sell, for their menus, to the interface's.
        void load_carried(render::Device& device, ParkScene& scene, ui::InterfaceAssets& assets)
        {
            scene.carried_read = true;
            const auto folder = assets.install ? assets.install->find("CarriedItems") : std::nullopt;
            if (!folder)
                return;
            std::vector<std::string> files;
            for (const std::filesystem::path& path : rct3::park::carried_overlays(*folder))
                files.push_back(std::format("{}/{}", folder->filename().string(), path.filename().string()));
            const std::vector<std::string_view> names(files.begin(), files.end());
            const std::size_t skins = ui::add_extra_skins(device, assets, names);
            logging::logger("park").info("{} carried items, {} extras, {} pictures", scene.carried.items.size(), scene.carried.extras.size(), skins);
        }

        /// The saved group of shown person `shown`, when the save has them in one.
        std::optional<const save::model::GuestGroup&> saved_group(const ParkScene& scene, std::size_t shown)
        {
            const ShownPerson& p = scene.shown_people[shown];
            if (p.staff || p.saved >= scene.people.guests.size() || !scene.people.guests[p.saved].group)
                return std::nullopt;
            const auto group = std::ranges::find(scene.people.groups, scene.people.guests[p.saved].group.id, &save::model::GuestGroup::id);
            return group != scene.people.groups.end() ? std::optional<const save::model::GuestGroup&>(*group) : std::nullopt;
        }

        /// The party shown person `shown` goes round with, as shown people, its leader first: the simulation's
        /// while it walks them, else the save's group; only them when they are on their own.
        std::vector<std::size_t> party_of(const ParkScene& scene, std::size_t shown)
        {
            std::vector<std::size_t> party;
            const auto add = [&](auto&& matches) {
                const auto at = std::ranges::find_if(scene.shown_people, matches);
                if (at != scene.shown_people.end() && !std::ranges::contains(party, static_cast<std::size_t>(at - scene.shown_people.begin())))
                    party.push_back(static_cast<std::size_t>(at - scene.shown_people.begin()));
            };
            const auto& r = scene.sim.registry;
            const entt::entity guest = scene.shown_people[shown].guest;
            if (const auto* member = guest != entt::null && r.valid(guest) ? r.try_get<simulation::Member>(guest) : nullptr; member != nullptr && r.valid(member->party))
            {
                const auto& walking = r.get<simulation::Party>(member->party);
                add([&](const ShownPerson& p) { return p.guest == walking.leader; });
                for (const entt::entity e : walking.members)
                    if (r.valid(e) && !r.all_of<simulation::Left>(e))
                        add([&](const ShownPerson& p) { return p.guest == e; });
            }
            else if (const auto group = saved_group(scene, shown))
            {
                const auto saved_as = [&](save::model::PeopleRef ref) { return [&scene, ref](const ShownPerson& p) { return !p.staff && p.saved < scene.people.guests.size() && scene.people.guests[p.saved].person.id == ref.id; }; };
                if (group->leader)
                    add(saved_as(group->leader));
                for (const save::model::PeopleRef ref : group->members)
                    if (ref)
                        add(saved_as(ref));
            }
            if (!std::ranges::contains(party, shown))
                party.push_back(shown);
            return party;
        }

        ui::screens::PartyFacts party_facts(const ParkScene& scene)
        {
            ui::screens::PartyFacts f;
            for (const std::size_t shown : scene.shown_party)
                f.members.push_back(person_facts(scene, shown));
            if (const auto group = saved_group(scene, scene.shown_party.front()))
                f.kind = group->group_type;
            return f;
        }

        void show_info_page(ParkScene& scene, ui::screens::InfoPanel& panel, const ui::InterfaceAssets& assets, const config::GameOptions& options)
        {
            if (!scene.shown_party.empty())
                panel.show_page(ui::screens::party_page(scene.party_tab, party_facts(scene), *assets.strings, info_units(assets, options)));
            else if (scene.shown_prop)
            {
                ui::screens::SceneryFacts f = prop_facts(scene, *scene.shown_prop);
                if (f.pool)
                    f.pool->currency = info_units(assets, options).currency;
                panel.show_page(ui::screens::scenery_page(scene.prop_tab, f, *assets.strings));
            }
            else if (scene.shown_person)
                panel.show_page(ui::screens::person_page(scene.person_tab, person_facts(scene, *scene.shown_person), *assets.strings, info_units(assets, options)));
            else if (scene.shown)
            {
                // A tracked ride's train limits are read the first time its window shows them.
                if (scene.shown->kind == rct3::park::AttractionRef::Kind::Ride && assets.install)
                    ready_trains(scene, *assets.install, scene.shown->index);
                panel.show_page(ui::screens::attraction_page(scene.info_tab, facts(scene, *scene.shown), *assets.strings, info_units(assets, options), scene.info_section));
            }
        }

        /// The round view of a person: looking at them from where the player looks from, turning from there.
        rct3::park::ParkCamera person_view(const ParkScene& scene, std::size_t shown, float yaw)
        {
            rct3::park::ParkCamera camera;
            camera.target = glm::vec3(scene.shown_people[shown].pose.world[3]) + glm::vec3(0, kPersonViewHeight, 0);
            camera.yaw = yaw;
            camera.pitch = kViewPitch;
            camera.distance = kPersonViewDistance;
            return camera;
        }

        /// Opens the panel on the person `shown` (of the shown people), at the tab last opened on a person if
        /// they have it.
        void open_person(ParkScene& scene, ui::screens::InfoPanel& panel, std::size_t shown, const ui::InterfaceAssets& assets, const config::GameOptions& options)
        {
            if (shown >= scene.shown_people.size())
                return;
            std::vector<ui::screens::InfoTab> tabs;
            scene.person_tabs.clear();
            std::size_t picked = 0;
            const ui::screens::PersonFacts f = person_facts(scene, shown);
            for (const ui::screens::PersonTabInfo& t : ui::screens::person_tabs(f))
            {
                if (t.tab == scene.person_tab)
                    picked = tabs.size();
                ui::screens::InfoTab& tab = tabs.emplace_back();
                tab.icon = t.icon;
                tab.help = assets.strings->get(t.help);
                tab.slot = t.slot;
                // A uniform's pots show the colours the person is drawn in.
                for (std::size_t pot = 0; pot < t.pots; ++pot)
                    tab.paints.push_back(scene.shown_people[shown].pose.look.colours.at(ui::screens::kUpperPalette + pot));
                tab.colours = ui::screens::kUniformColours;
                tab.action = t.tab == ui::screens::PersonTab::Party;
                scene.person_tabs.push_back(t.tab);
            }
            // A uniform tab has no page of its own, nor the party's, which opens the party.
            if (scene.person_tabs.at(picked) == ui::screens::PersonTab::Uniform || scene.person_tabs.at(picked) == ui::screens::PersonTab::Party)
                picked = 0;
            scene.person_tab = scene.person_tabs.at(picked);
            if (scene.shown_person != shown)
            {
                scene.view_camera = person_view(scene, shown, scene.camera.yaw);
                scene.painting_patrol = false;
            }
            scene.shown.reset();
            scene.shown_prop.reset();
            scene.shown_party.clear();
            scene.shown_person = shown;
            panel.show(std::move(tabs), picked);
            show_info_page(scene, panel, assets, options);
        }

        /// Opens the panel on the party shown person `shown` goes round with, at the tab last opened on a party.
        void open_party(ParkScene& scene, ui::screens::InfoPanel& panel, std::size_t shown, const ui::InterfaceAssets& assets, const config::GameOptions& options)
        {
            std::vector<ui::screens::InfoTab> tabs;
            std::size_t picked = 0;
            for (const ui::screens::PartyTabInfo& t : ui::screens::kPartyTabs)
            {
                if (t.tab == scene.party_tab)
                    picked = tabs.size();
                tabs.push_back({ .icon = std::string(t.icon), .help = std::u16string(assets.strings->get(t.help)), .slot = t.slot });
            }
            scene.shown_party = party_of(scene, shown);
            scene.shown.reset();
            scene.shown_person.reset();
            scene.shown_prop.reset();
            panel.show(std::move(tabs), picked);
            show_info_page(scene, panel, assets, options);
        }

        /// Opens the panel on the attraction `ref`, at `tab`, else the tab last opened, if it has one.
        void open_info(ParkScene& scene, ui::screens::InfoPanel& panel, rct3::park::AttractionRef ref, const ui::InterfaceAssets& assets, const config::GameOptions& options, std::optional<ui::screens::AttractionTab> tab = std::nullopt)
        {
            const ui::screens::AttractionFacts f = facts(scene, ref);
            if (f.common == nullptr)
                return;
            const ui::screens::AttractionKind kind = f.stall ? ui::screens::AttractionKind::Stall : ui::screens::attraction_kind(*f.ride);
            if (const bool tracked = f.ride != nullptr && f.ride->kind == save::model::RideKind::Tracked; tracked != scene.info_tracked)
            {
                std::swap(scene.info_tab, scene.other_info_tab);
                scene.info_tracked = tracked;
            }
            if (tab)
                scene.info_tab = *tab;
            std::vector<ui::screens::InfoTab> tabs;
            scene.shown_tabs.clear();
            std::size_t picked = 0;
            for (const ui::screens::AttractionTabInfo& t : ui::screens::attraction_tabs(kind, f.common->state))
            {
                if (t.tab == scene.info_tab)
                    picked = tabs.size();
                ui::screens::InfoTab& tab = tabs.emplace_back();
                tab.icon = t.icon;
                tab.help = assets.strings->get(t.help);
                tab.slot = t.slot;
                tab.action = t.action;
                if (t.tab == ui::screens::AttractionTab::State)
                    tab.choices = state_fan(scene, ref, *assets.strings);
                scene.shown_tabs.push_back(t.tab);
            }
            // The open-or-close tab has no page of its own.
            if (scene.shown_tabs.at(picked) == ui::screens::AttractionTab::State)
                picked = 0;
            scene.info_tab = scene.shown_tabs.at(picked);
            if (scene.shown != ref || scene.shown_person || !scene.shown_party.empty())
            {
                // The view starts from where the player looks from, and turns from there.
                std::vector<glm::vec3> points;
                for (std::size_t i = 0; i < scene.objects.size() && i < scene.anchors.size(); ++i)
                {
                    const auto owner = scene.attractions.find(scene.objects[i].id);
                    if (owner != scene.attractions.end() && owner->second == ref)
                        points.push_back(scene.anchors[i]);
                }
                scene.view_camera = rct3::park::framed(points, scene.camera.yaw, kViewPitch);
            }
            scene.shown_person.reset();
            scene.shown_prop.reset();
            scene.shown_party.clear();
            if (scene.shown != ref)
                scene.refusal.clear();
            scene.shown = ref;
            panel.show(std::move(tabs), picked);
            show_info_page(scene, panel, assets, options);
        }

        /// Opens the panel on the prop `index` (of the objects), with only its name showing, or on its tab `open`.
        void open_prop(ParkScene& scene, ui::screens::InfoPanel& panel, std::size_t index, const ui::InterfaceAssets& assets, const config::GameOptions& options, std::optional<ui::screens::SceneryTab> open = std::nullopt)
        {
            if (index >= scene.objects.size())
                return;
            std::vector<ui::screens::InfoTab> tabs;
            scene.prop_tabs.clear();
            for (const ui::screens::SceneryTabInfo& t : ui::screens::scenery_tabs(prop_facts(scene, index)))
            {
                ui::screens::InfoTab& tab = tabs.emplace_back();
                tab.icon = t.icon;
                tab.help = assets.strings->get(t.help);
                tab.slot = t.slot;
                tab.action = t.action;
                tab.paints = t.paints;
                // A changing room's flag opens or closes its complex, as a stall's does.
                if (t.tab == ui::screens::SceneryTab::RoomState)
                    for (const ui::screens::StateChoice& c : ui::screens::state_choices(ui::screens::AttractionKind::Stall))
                        tab.choices.push_back({ std::string(c.icon), std::u16string(assets.strings->get(c.help)) });
                scene.prop_tabs.push_back(t.tab);
            }
            if (scene.shown_prop != index)
                scene.view_camera = rct3::park::framed(rct3::park::object::object_corners(scene.objects[index], scene.models, scene.grid, scene.fitted), scene.camera.yaw, kViewPitch, kPropViewClosest);
            scene.shown.reset();
            scene.shown_person.reset();
            scene.shown_party.clear();
            scene.shown_prop = index;
            const auto picked = open ? std::ranges::find(scene.prop_tabs, *open) : scene.prop_tabs.end();
            scene.prop_tab = picked != scene.prop_tabs.end() ? open : std::nullopt;
            const auto at = static_cast<std::size_t>(picked - scene.prop_tabs.begin());
            panel.show(std::move(tabs), at);
            show_info_page(scene, panel, assets, options);
        }

    } // namespace

    namespace
    {

        /// Paints colour slot `slot` of the prop `index` in colour `colour` of the 32.
        void paint_prop(render::Device& device, ParkScene& scene, std::size_t index, std::size_t slot, std::int32_t colour)
        {
            if (slot < rct3::park::kFlexiSlotCount)
                change_object(device, scene, index, [&](save::model::PlacedObject& o) { o.colours.at(slot) = colour; });
        }

        constexpr float kRidePullPerNotch = 0.5f; ///< Of the ride camera's pull back, per wheel notch
        constexpr float kRidePullMost = 3;        ///< The most it pulls back, which is squared into metres

        /// Writes a change the player made on an attraction's page into the save. Nothing is shown again, so a
        /// spinner held down keeps stepping; a page that looks different after the change is set up here.
        void change_attraction(render::Device& device, ParkScene& scene, ui::screens::InfoPanel& panel, rct3::park::AttractionRef ref, const ui::screens::InfoChange& change, const ui::InterfaceAssets& assets, const config::GameOptions& options)
        {
            using enum ui::screens::AttractionControl;
            save::model::Ride* ride = ref.kind == rct3::park::AttractionRef::Kind::Ride && ref.index < scene.rides.rides.size() ? &scene.rides.rides[ref.index] : nullptr;
            save::model::Stall* stall = ref.kind == rct3::park::AttractionRef::Kind::Stall && ref.index < scene.rides.stalls.size() ? &scene.rides.stalls[ref.index] : nullptr;
            const auto wait_on = [&](std::int32_t& wait, ui::screens::AttractionControl seconds) {
                const auto field = panel.control(ui::screens::control_id(seconds));
                wait = change.value != 0 ? (field ? static_cast<const ui::widgets::Spinner&>(*field).value() : 0) : -1;
                if (field)
                    field->enabled = change.value != 0;
            };
            if (change.id == ui::screens::control_id(Colours))
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
            else if (change.id == ui::screens::control_id(Name))
            {
                // A blank name keeps the old one, as in the game.
                if (save::model::AttractionCommon* common = ride != nullptr ? &ride->base : stall != nullptr ? &stall->base : nullptr; common != nullptr && change.text.find_first_not_of(u' ') != std::u16string::npos)
                    common->name = strings::to_utf8(change.text);
            }
            else if (change.id == ui::screens::control_id(Locate))
            {
                // The view turns round to the middle of the attraction's objects.
                glm::vec3 sum { };
                std::size_t count = 0;
                for (const save::model::PlacedObject& o : scene.objects)
                {
                    const auto owner = scene.attractions.find(o.id);
                    if (owner == scene.attractions.end() || owner->second != ref)
                        continue;
                    const auto model = scene.models.find(rct3::park::object::model_ref(o));
                    sum += glm::vec3(rct3::park::object::placed_transform(o, model == scene.models.end() ? nullptr : &model->second, scene.grid, scene.fitted)[3]);
                    ++count;
                }
                if (count > 0)
                    scene.camera.target = sum / static_cast<float>(count);
            }
            else if (change.id == ui::screens::control_id(RideCam) && ride != nullptr)
                ride_cam(scene, assets, ref.index);
            else if (stall != nullptr)
            {
                const std::size_t index = ref.index;
                if (change.id == ui::screens::control_id(SyncStalls))
                    stall->sync = change.value != 0;
                else if (const auto item = ui::screens::listed(change.id, ItemPrice); item && *item < stall->items.size())
                    stall->items[*item].price = change.value;
                else if (const auto gone = ui::screens::listed(change.id, ItemRemove); gone && *gone < stall->items.size())
                    stall->items.erase(stall->items.begin() + static_cast<std::ptrdiff_t>(*gone));
                else if (const auto added = ui::screens::listed(change.id, ItemAdd))
                {
                    const rct3::park::BuildItem* kind = stall_kind(scene, *stall);
                    if (kind == nullptr || *added >= kind->goods.size())
                        return;
                    const rct3::park::BuildItem::Goods& g = kind->goods[*added];
                    // shortcut: it goes on at its kind's own price, as a new stall's items do; upgrade alongside them.
                    const auto at = stall->items.begin() + std::clamp<std::ptrdiff_t>(change.value, 0, std::ssize(stall->items));
                    stall->items.insert(at, { .kind_symbol = std::format("{}:cid", g.item), .price = g.price, .name = "%ME%" });
                }
                else if (const auto moved = ui::screens::listed(change.id, ItemMove); moved && *moved < stall->items.size())
                    rct3::park::ride::move_item(*stall, *moved, static_cast<std::size_t>(std::max(change.value, 0)));
                else if (const auto ticked = ui::screens::listed(change.id, ItemExtras); ticked && *ticked < stall->items.size())
                    stall->items[*ticked].extras = static_cast<std::uint32_t>(change.value);
                else if (const auto named = ui::screens::listed(change.id, ItemName); named && *named < stall->items.size())
                    stall->items[*named].name = strings::to_utf8(change.text);
                else if (const auto painted = ui::screens::listed(change.id, ItemColour); painted && *painted < stall->items.size() && change.pot < save::model::RideColours { }.size())
                    stall->items[*painted].colours[change.pot] = static_cast<std::uint8_t>(change.value);
                else if (const auto extra = ui::screens::listed(change.id, ExtraLevel); extra && *extra < stall->extras.size())
                    stall->extras[*extra].level = change.value;
                else
                    return;
                if (stall->sync)
                    rct3::park::ride::copy_menu(scene.rides.stalls, index);
                for (std::size_t i = 0; i < scene.rides.stalls.size(); ++i)
                {
                    save::model::Stall& s = scene.rides.stalls[i];
                    if (i != index && !(stall->sync && s.base.symbol == stall->base.symbol && s.base.overlay_path == stall->base.overlay_path))
                        continue;
                    // shortcut: a stall left with nothing to sell closes; the game's way with it is not known,
                    // upgrade once it is.
                    if (s.items.empty() && s.base.state == save::model::AttractionState::Open)
                        set_attraction_state(scene, s.base, save::model::AttractionState::Closed);
                    // An item put on brings the extras it takes.
                    rct3::park::ride::offer_extras(s, scene.carried.items);
                    simulation::restock(scene.sim, s, scene.carried);
                }
                // The menu's rows follow what changed; a price typed in stays as it is.
                if (!ui::screens::listed(change.id, ItemPrice))
                    show_info_page(scene, panel, assets, options);
            }
            else if (ride == nullptr)
                return;
            else if (change.id == ui::screens::control_id(Price))
                ride->ticket_price = change.value;
            else if (change.id == ui::screens::control_id(Inspection))
            {
                ride->breakdown.inspection_interval = change.value;
                if (const entt::entity e = ref.index < scene.ride_entities.size() ? scene.ride_entities[ref.index] : entt::null; e != entt::null)
                    if (auto* upkeep = scene.sim.registry.try_get<simulation::Upkeep>(e))
                        upkeep->inspect_every = simulation::upkeep_of(*ride).inspect_every;
            }
            else if (change.id == ui::screens::control_id(Mechanic))
            {
                // shortcut: the game also opens the mechanic's window and puts this job first on a busy one's list;
                // add both with the staff window's job list.
                if (const entt::entity e = ref.index < scene.ride_entities.size() ? scene.ride_entities[ref.index] : entt::null; e != entt::null)
                    if (auto* upkeep = scene.sim.registry.try_get<simulation::Upkeep>(e))
                        upkeep->called = true;
            }
            else if (change.id == ui::screens::control_id(MinWaitOn))
                wait_on(ride->options.min_wait, MinWait);
            else if (change.id == ui::screens::control_id(MaxWaitOn))
                wait_on(ride->options.max_wait, MaxWait);
            else if (change.id == ui::screens::control_id(MinWait))
                ride->options.min_wait = change.value;
            else if (change.id == ui::screens::control_id(MaxWait))
                ride->options.max_wait = change.value;
            else if (change.id == ui::screens::control_id(WaitFor))
                ride->options.wait_for_load = change.value;
            else if ((change.id == ui::screens::control_id(Trains) || change.id == ui::screens::control_id(Cars)) && scene.train_limits.contains(ref.index) && ride->base.state != save::model::AttractionState::Open && assets.install)
            {
                const bool trains = change.id == ui::screens::control_id(Trains);
                (void)set_trains(device, scene, *assets.install, ref.index, trains ? change.value : ride->train_count, trains ? ride->cars_per_train : change.value);
            }
            else if (change.id == ui::screens::control_id(CarKind) && scene.train_limits.contains(ref.index) && ride->base.state != save::model::AttractionState::Open && assets.install && !ride->trains.empty())
            {
                // The next kind of train its kind runs, round to the first after the last.
                const std::vector<std::string>& choices = scene.train_choices[ref.index];
                const std::string_view symbol = ride->trains.front().symbol;
                const std::string_view now = symbol.substr(0, symbol.find(':'));
                const auto at = std::ranges::find_if(choices, [&](const std::string& c) { return strings::equals_ignoring_case(c, now); });
                if (choices.size() > 1)
                    if (use_train(scene, *assets.install, ref.index, choices[at == choices.end() ? 0 : static_cast<std::size_t>(at - choices.begin() + 1) % choices.size()]))
                    {
                        (void)set_trains(device, scene, *assets.install, ref.index, ride->train_count, ride->cars_per_train);
                        update_platforms(scene, ref.index, assets);
                    }
            }
            else if (change.id == ui::screens::control_id(Circuits))
            {
                // A go under way runs as many times round as the new count, or stops if it is past it.
                // shortcut: a tracked ride keeps the count but its trains go round once; run the laps once the
                // trains count them.
                ride->loop_count = change.value;
                if (const entt::entity e = ref.index < scene.ride_entities.size() ? scene.ride_entities[ref.index] : entt::null; e != entt::null)
                    if (auto* cycle = scene.sim.registry.try_get<simulation::CycleRide>(e))
                        cycle->loops = static_cast<std::uint32_t>(std::max(change.value, 1));
            }
            else if (const auto show = std::ranges::find(scene.shows, ride->base.id, &save::model::RideShow::ride); show != scene.shows.end() && (change.id == ui::screens::control_id(ShowTrick) || change.id == ui::screens::control_id(ShowDelete)))
            {
                // The ride puts the changed show on from its next go.
                const std::size_t place = std::min(scene.info_section, show->order.size());
                const std::vector<std::uint32_t> offers = ui::screens::show_offers(*show);
                if (change.id == ui::screens::control_id(ShowDelete) && place < show->order.size() && show->order.size() > 1)
                    show->order.erase(show->order.begin() + static_cast<std::ptrdiff_t>(place));
                else if (change.id == ui::screens::control_id(ShowTrick) && change.value >= 0 && std::cmp_less(change.value, offers.size()))
                {
                    if (place < show->order.size())
                        show->order[place] = offers[static_cast<std::size_t>(change.value)];
                    else
                        show->order.push_back(offers[static_cast<std::size_t>(change.value)]);
                }
                stage_show(scene, *show);
            }
            else if (change.id == ui::screens::control_id(BlockSection))
            {
                // shortcut: the game also caps the trains at one fewer than the blocks; upgrade with the trains'
                // limits counting blocks.
                ride->options.block_sectioned = change.value != 0;
                if (const entt::entity e = ref.index < scene.ride_entities.size() ? scene.ride_entities[ref.index] : entt::null; e != entt::null)
                    if (auto* fleet = scene.sim.registry.try_get<simulation::Trains>(e))
                        for (simulation::Coaster& c : fleet->trains)
                            c.blocks = ride->options.block_sectioned;
            }
            else if (change.id == ui::screens::control_id(SyncStations))
                ride->options.sync_trains = change.value != 0; // shortcut: stored, stations do not wait for each other yet
            else if (change.id == ui::screens::control_id(LeaveOnArrival))
                ride->options.leave_if_other_train_enters_station = change.value != 0;
            else if (change.id == ui::screens::control_id(LowFriction))
            {
                if (change.value != 0)
                    scene.low_friction.insert(ride->base.id);
                else
                    scene.low_friction.erase(ride->base.id);
            }
            else if (change.id == ui::screens::control_id(LaunchSpeed) || change.id == ui::screens::control_id(LiftSpeed))
            {
                // The page shows hundredths of the player's unit.
                const float speed = static_cast<float>(change.value) / 100 / ui::screens::speed_scale(info_units(assets, options).distance);
                if (change.id == ui::screens::control_id(LaunchSpeed))
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
            else if (change.id >= ui::screens::control_id(SpeedSeries) && change.id <= ui::screens::control_id(LateralSeries))
            {
                const auto bit = static_cast<std::uint8_t>(1u << (change.id - ui::screens::control_id(SpeedSeries)));
                std::uint8_t& shown = scene.graph_series.try_emplace(ride->base.id, ui::screens::kSpeedOnly).first->second;
                shown = static_cast<std::uint8_t>(change.value != 0 ? shown | bit : shown & ~bit);
            }
            else if (change.id == ui::screens::control_id(Music))
                scene.ride_tunes[ride->base.id] = static_cast<std::size_t>(std::max(change.value, 0));
            else if (change.id == ui::screens::control_id(QueueSigns))
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

        /// Hires a worker for job `kind` from the staff window and returns their place in shown_people, to be
        /// handed to the pointer as the game hands them; nothing where none can be hired. A keeper goes to an
        /// animal house picked at random instead, which dresses them with the other keepers.
        std::optional<std::size_t> hire(ParkScene& scene, save::model::StaffKind kind)
        {
            using enum save::model::StaffKind;
            if (!scene.simulate || kind == Vendor || kind == LifeGuard || kind == ParkInspector)
                return std::nullopt;
            auto& r = scene.sim.registry;
            if (kind == AnimalKeeper)
            {
                const auto houses = r.view<const simulation::AnimalHouse>();
                std::vector<entt::entity> homes(houses.begin(), houses.end());
                if (!homes.empty())
                {
                    const entt::entity home = homes[random::below(scene.looks, homes.size())];
                    (void)simulation::add_keeper(scene.sim, home, r.get<const simulation::AnimalHouse>(home).troughs);
                }
                return std::nullopt;
            }
            const auto type = kind == Handyman ? save::model::PersonType::Handyman : kind == Mechanic ? save::model::PersonType::Mechanic : kind == SecurityGuard ? save::model::PersonType::SecurityGuard : save::model::PersonType::Entertainer;
            const auto role = simulation::staff_role(kind);
            // Each entertainer the next costume in turn, as the game gives them out.
            std::vector<std::uint8_t> costumes;
            for (const Outfit& o : scene.wardrobe)
                if (o.type == type && !std::ranges::contains(costumes, o.costume))
                    costumes.push_back(o.costume);
            std::ranges::sort(costumes);
            const std::uint8_t costume = costumes.empty() ? 0 : costumes[scene.costumes_given++ % costumes.size()];
            std::array<const Outfit*, 3> worn { };
            for (const Outfit& o : scene.wardrobe)
                if (o.type == type && o.costume == costume && !o.swim && o.section < worn.size() && worn[o.section] == nullptr)
                    worn[o.section] = &o;
            // On a walkway picked at random, as the game puts them on one of the park's paths.
            std::vector<simulation::NodeId> walkways;
            for (simulation::NodeId n = 0; n < scene.sim.paths.nodes().size(); ++n)
                if (const simulation::PathNode& node = scene.sim.paths.nodes()[n]; !node.removed && !node.queue && !node.staff_kept_off)
                    walkways.push_back(n);
            if (std::ranges::contains(worn, nullptr) || walkways.empty())
                return std::nullopt;
            const entt::entity e = simulation::add_staff(scene.sim, role, walkways[random::below(scene.looks, walkways.size())]);
            r.get<simulation::Staff>(e).training = simulation::hired_training(scene.park.cash.has_value());
            scene.known_guests.insert(e);
            change_people(scene);
            std::int32_t number = 0;
            for (const save::model::Staff& s : scene.people.staff)
                if (s.kind == kind)
                    number = std::max(number, s.type_id);
            const simulation::Staff& worker = r.get<const simulation::Staff>(e);
            const glm::vec3 at = r.get<const simulation::Pose>(e).position;
            save::model::Staff& kept = scene.people.staff.emplace_back();
            kept.kind = kind;
            kept.type_id = number + 1;
            kept.wage_level = worker.wage_level;
            kept.training = worker.training;
            kept.happiness = worker.happiness.value;
            kept.task_flags = worker.tasks;
            kept.person.type = type;
            kept.person.sub_type = costume;
            kept.person.name = std::format("%ME% {}", kept.type_id); // As the game names them, after their job
            kept.person.start_time = scene.park.time;                // Hired today, and their work counted from nothing
            if (kind == save::model::StaffKind::Handyman)
                kept.work = save::model::HandymanWork { };
            else if (kind == save::model::StaffKind::Mechanic)
                kept.work = save::model::MechanicWork { };
            kept.person.position = { at.x, at.y, at.z };
            save::model::Appearance& a = kept.person.appearance;
            a.head = { worn[0]->id };
            a.body = { worn[1]->id };
            a.legs = { worn[2]->id };
            ShownPerson& shown = scene.shown_people.emplace_back(ShownPerson { .walking = true, .staff = true, .saved = scene.people.staff.size() - 1, .guest = e });
            shown.pose.look.head = worn[0]->set;
            shown.pose.look.body = worn[1]->set;
            shown.pose.look.legs = worn[2]->set;
            for (std::size_t i = 0; i < shown.pose.look.colours.size(); ++i)
                shown.pose.look.colours[i] = a.palettes[i] = static_cast<std::uint8_t>(random::below(scene.looks, kColourCells));
            shown.pose.motion = rct3::park::person::motion::kWalk;
            shown.pose.world = glm::translate(glm::mat4(1.0f), at);
            return scene.shown_people.size() - 1;
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
        bool change_person(ParkScene& scene, std::size_t shown, const ui::screens::InfoChange& change)
        {
            using enum ui::screens::PersonControl;
            if (shown >= scene.shown_people.size())
                return false;
            const ShownPerson& p = scene.shown_people[shown];
            save::model::Staff* staff = p.staff && p.saved < scene.people.staff.size() ? &scene.people.staff[p.saved] : nullptr;
            save::model::Guest* guest = !p.staff && p.saved < scene.people.guests.size() ? &scene.people.guests[p.saved] : nullptr;
            save::model::Person* person = staff != nullptr ? &staff->person : guest != nullptr ? &guest->person : nullptr;
            if (person == nullptr)
                return false;
            if (change.id == ui::screens::person_control(Name))
            {
                // A blank name keeps the old one.
                if (change.text.empty())
                    return true;
                person->name = strings::to_utf8(change.text);
                return true;
            }
            if (change.id == ui::screens::person_control(ToConsole))
            {
                person->thoughts_to_console = !person->thoughts_to_console;
                return true;
            }
            if (change.id == ui::screens::person_control(Highlight))
            {
                person->highlight = !person->highlight;
                return true;
            }
            if (staff == nullptr)
                return false;
            // The details page shows the tools for painting while it goes on.
            if (change.id == ui::screens::person_control(PatrolPaint))
            {
                scene.painting_patrol = change.value != 0;
                return true;
            }
            if (change.id == ui::screens::person_control(PatrolEdit))
            {
                scene.painting_patrol = !scene.painting_patrol;
                return true;
            }
            if (const entt::entity e = shown_staff(scene); e != entt::null && change.id == ui::screens::person_control(PatrolClear))
            {
                simulation::clear_patrol(scene.sim.registry, e);
                return true;
            }
            else if (e != entt::null && change.id == ui::screens::person_control(PatrolFill))
            {
                simulation::fill_patrol(scene.sim.registry, e);
                return true;
            }
            else if (e != entt::null && change.id == ui::screens::person_control(PatrolZone))
            {
                simulation::join_zone(scene.sim.registry, e, static_cast<std::uint32_t>(std::max(change.value, 0)));
                return true;
            }
            if (change.id == ui::screens::person_control(PatrolBrush))
            {
                scene.patrol_brush = change.value;
                return false;
            }
            if (change.id == ui::screens::person_control(PatrolCopy))
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
            if (change.id == ui::screens::person_control(Warn))
            {
                if (care.warnings < ui::screens::kMostWarnings)
                    simulation::warn(care);
                return show();
            }
            if (change.id == ui::screens::person_control(Train))
            {
                // Paid for at once, free in a park without money; none when the park cannot afford it.
                const std::int64_t cost = simulation::training_cost(care);
                if (scene.park.cash && *scene.park.cash < cost)
                    return false;
                if (simulation::train(care) && scene.park.cash)
                {
                    *scene.park.cash -= cost;
                    simulation::park::ledger_month(scene.park_model.finance, scene.ledger_month, scene.park.date).staff_training += static_cast<std::int32_t>(cost);
                }
                return show();
            }
            if (change.id == ui::screens::person_control(Wage))
            {
                simulation::set_wage_level(care, change.value);
                (void)show();
            }
            else if (change.id == ui::screens::person_control(Costume))
            {
                person->sub_type = static_cast<std::uint8_t>(change.value);
                // They change into it, where the install has it.
                std::array<const Outfit*, 3> worn { };
                for (const Outfit& o : scene.wardrobe)
                    if (o.type == save::model::PersonType::Entertainer && o.costume == person->sub_type && o.section < worn.size())
                        worn[o.section] = &o;
                if (!std::ranges::contains(worn, nullptr))
                {
                    change_people(scene);
                    rct3::park::person::PersonLook& look = scene.shown_people[shown].pose.look;
                    std::tie(look.head, look.body, look.legs) = std::tuple(worn[0]->set, worn[1]->set, worn[2]->set);
                    std::tie(person->appearance.head, person->appearance.body, person->appearance.legs) = std::tuple(save::model::PeopleRef { worn[0]->id }, save::model::PeopleRef { worn[1]->id }, save::model::PeopleRef { worn[2]->id });
                }
            }
            else if (change.id >= ui::screens::person_control(Task))
            {
                staff->task_flags ^= 1 << (change.id - ui::screens::person_control(Task));
                care.tasks = staff->task_flags;
            }
            return false;
        }

        /// Paints the clothes of pot `pot` of the person shown as `shown` in colour `colour`.
        void paint_person(ParkScene& scene, std::size_t shown, std::size_t pot, std::size_t colour)
        {
            if (shown >= scene.shown_people.size() || ui::screens::kUpperPalette + pot >= rct3::park::person::PersonLook { }.colours.size())
                return;
            change_people(scene);
            ShownPerson& p = scene.shown_people[shown];
            const auto c = static_cast<std::uint8_t>(colour);
            p.pose.look.colours.at(ui::screens::kUpperPalette + pot) = c;
            save::model::Person* person = p.staff ? &scene.people.staff.at(p.saved).person : &scene.people.guests.at(p.saved).person;
            person->appearance.palettes.at(ui::screens::kUpperPalette + pot) = c;
        }

        /// Whether the object is one of the park's entrances, which a click opens the park's window on.
        bool park_entrance(const ParkScene& scene, const save::model::PlacedObject& o)
        {
            const auto model = scene.models.find(rct3::park::object::model_ref(o));
            return !o.hidden && model != scene.models.end() && model->second.category == std::to_underlying(rct3::park::ItemCategory::ParkEntrance);
        }

        /// What putting a broken path item back new costs: the price of the object it stands as.
        std::int64_t replace_price(const ParkScene& scene, std::uint64_t object)
        {
            const auto o = std::ranges::find(scene.objects, object, &save::model::PlacedObject::id);
            const auto model = o != scene.objects.end() ? scene.models.find(rct3::park::object::model_ref(*o)) : scene.models.end();
            return model != scene.models.end() ? model->second.price : 0;
        }

        ui::screens::ManagementFacts management_facts(const ParkScene& scene)
        {
            const auto broken = simulation::replacement(scene.sim, [&](std::uint64_t object) { return replace_price(scene, object); });
            return { .park = &scene.park_model, .state = &scene.park, .people = &scene.people, .rides = &scene.rides, .broken_items = broken.items, .replace_cost = broken.cost, .goals = scene.goal_readings, .animal_houses = !scene.sim.registry.view<simulation::AnimalHouse>().empty(), .staff_looks = scene.staff_looks, .zero_time = scene.park.zero_time };
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

        /// Opens the info panel on what a management window's row is about, if the park shows it.
        void open_row(ParkScene& scene, ui::screens::InfoPanel& panel, ui::screens::ManagementRow row, const ui::InterfaceAssets& assets, const config::GameOptions& options)
        {
            using Kind = ui::screens::ManagementRow::Kind;
            if (row.kind == Kind::Ride || row.kind == Kind::Stall)
            {
                open_info(scene, panel, { row.kind == Kind::Stall ? rct3::park::AttractionRef::Kind::Stall : rct3::park::AttractionRef::Kind::Ride, row.index }, assets, options);
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
            for (const auto [_, patrol] : scene.sim.registry.view<const simulation::Patrol>().each())
                shown.patrol_tiles.insert(shown.patrol_tiles.end(), patrol.tiles.begin(), patrol.tiles.end());
            return shown;
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
        // Those the view and the window follow keep to them; one gone takes them away.
        for (std::optional<std::size_t>* followed : { &scene.shown_person, &scene.riding_person })
        {
            if (*followed == shown)
                followed->reset();
            else if (*followed && **followed > shown)
                --**followed;
        }
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

    void update_park_scene(render::Device& device, ParkScene& scene, ui::InterfaceAssets& assets, const config::GameOptions& options, const ParkInput& input, const ui::KeyboardState& keyboard, float elapsed)
    {
        // The ground is built before the models that give the rides' and stalls' paving its size are read.
        if (scene.footpaths.pavings_stale)
            profiling::section("Ground", [&] { rebuild_ground(device, scene, assets); });
        profiling::section("LedSigns", [&] { update_led_signs(device, scene, assets); });
        const float scale = scale_for(device, scene);
        const render::Extent extent = device.extent();
        ui::Desktop& desktop = *scene.desktop;
        desktop.layout({ 0, 0, static_cast<float>(extent.width) / scale, static_cast<float>(extent.height) / scale });

        // Escape asks to leave, as the guide's quit does, unless the question is already open: then it
        // answers it. An open build list takes it first, to empty its search and then to close; a tool out is
        // put away, and a path trail going on let go, before anything closes.
        const bool asking = desktop.root().find(kQuestionName).has_value();
        ui::KeyboardState keys = keyboard;
        if (!asking && !build_list_open(desktop.root()))
        {
            const auto escape = std::ranges::remove_if(keys.presses, [](const ui::KeyPress& k) { return k.key == ui::Key::Escape; });
            auto tools = desktop.root().find_as<ui::screens::ToolRow>(kToolsName);
            if (!escape.empty() && scene.holding)
                hold_person(scene, { }, { }, false, true);
            else if (!escape.empty() && scene.riding_along())
                stop_riding(scene);
            else if (!escape.empty() && tools && tools->open_menu())
                tools->close_menu();
            else if (!escape.empty() && (ride_placing(scene) || scene.structures.tool != StructureTools::Tool::None || scene.deleting.tool != DeleteTools::Tool::None || scene.pooling.item || scene.enclosing.item || land_tool(scene)))
            {
                stop_ride_placing(scene);
                stop_structure_tools(scene);
                stop_deleting(scene);
                stop_pooling(scene);
                stop_enclosing(scene);
                stop_land_tools(scene);
            }
            else if (!escape.empty() && !drop_footpath_trail(scene) && !close_top_management_window(desktop))
                ask_leave(scene, *assets.strings, assets.palette);
            keys.presses.erase(escape.begin(), escape.end());
        }

        // A stall's window shows what it sells by name and picture once those are read.
        if (!scene.carried_read && scene.shown && scene.shown->kind == rct3::park::AttractionRef::Kind::Stall)
        {
            load_carried(device, scene, assets);
            if (auto info = desktop.root().find_as<ui::screens::InfoPanel>(kInfoName))
                show_info_page(scene, *info, assets, options);
        }

        // Tracked rides put to test or opened: their trains made ready, as the game does, and run round a whole
        // circuit, a shuttle's track, or an unfinished one to its end, where they crash. While the panel builds an
        // unfinished track, a test of it runs a ghost instead (track_building.hpp); closing the panel readies it again.
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
            if ((r.mode == save::model::OperatingMode::LazyRiver || (!coaster && !boats) || set_trains(device, scene, *assets.install, ride, r.train_count, r.cars_per_train)) && !ghosted && (ride >= scene.ride_entities.size() || scene.ride_entities[ride] == entt::null) && !std::ranges::contains(scene.placing.unjoined, ride))
                scene.placing.unjoined.push_back(ride);
            if (auto info = desktop.root().find_as<ui::screens::InfoPanel>(kInfoName); info && scene.shown && scene.shown->kind == rct3::park::AttractionRef::Kind::Ride && scene.shown->index == ride)
                show_info_page(scene, *info, assets, options);
        }

        ui::PointerState pointer = input.pointer;
        pointer.position = { pointer.position.x / scale, pointer.position.y / scale };
        scene.current_fonts = &ui::fonts_for(device, assets, desktop.root().rect().width());
        const bool over_interface = profiling::section("Interface", [&] { return desktop.update(pointer, keys, { elapsed, scene.current_fonts }); });
        // Riding along, the park takes no clicks: the whole screen counts as interface.
        const bool over = over_interface || scene.riding_along();
        if (auto panel = scene.riding_along() ? desktop.root().find_as<ui::screens::RideCameraPanel>(kRideCameraName) : std::nullopt)
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
        auto info = desktop.root().find_as<ui::screens::InfoPanel>(kInfoName);
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
            if (auto panel = desktop.root().find_as<ui::screens::DeleteAreaPanel>(kDeleteAreaName); panel && area)
            {
                panel->set_marked(deleting.corner.has_value());
                const auto asked = panel->take_action();
                if (asked == ui::screens::DeleteAreaAction::Delete && deleting.corner && deleting.other)
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
                scene.placing.solids_of = static_cast<std::size_t>(-1);
                scene.footpaths.solids_of = static_cast<std::size_t>(-1);
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
                else if (still && !over && (structure_tools_click(device, scene, assets) || ride_placing_click(device, scene, assets) || track_building_click(scene, assets, pointer.position, { screen.width(), screen.height() })))
                    ;
                else if (still && !over && !footpath_click(scene, pointer.position, { screen.width(), screen.height() }, input.control))
                {
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
                            open_management_window(desktop, ui::screens::ManagementTopic::ParkOperations, 0, management_facts(scene), *assets.strings, assets.palette, info_units(assets, options));
                        // A changing room's window opens on the tab last open in one.
                        if (pool_room(scene, o))
                            open_prop(scene, *info, *picked.prop, assets, options, scene.room_tab.value_or(ui::screens::SceneryTab::RoomDetails));
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
            // the delete menu's tool does, and the tool stays out; the other tools are put away. Escape puts any away.
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
                else if (scene.structures.tool != StructureTools::Tool::None)
                    stop_structure_tools(scene);
                else if (scene.deleting.tool == DeleteTools::Tool::Area)
                    stop_deleting(scene);
                else if (scene.pooling.item)
                {
                    // A pool tool takes up the pool square pointed at first.
                    if (!over && !delete_pool_square(device, scene, assets, pointer.position, { screen.width(), screen.height() }))
                        delete_under(pointer.position, { screen.width(), screen.height() });
                }
                else if (ride_placing(scene) || scene.deleting.tool == DeleteTools::Tool::One || scene.enclosing.item)
                {
                    if (!over)
                        delete_under(pointer.position, { screen.width(), screen.height() });
                }
                else if (land_tool(scene))
                {
                    if (!delete_waterfall(device, scene, assets, pointer.position, { screen.width(), screen.height() }))
                        stop_land_tools(scene);
                }
                else if (!over)
                    (void)footpath_sell(scene, pointer.position, { screen.width(), screen.height() });
            }
            // The customers page follows the running park; it has no controls to lose a half-made change in. Any page
            // shows again once a crash closed its ride.
            scene.since_info_page += elapsed;
            if (scene.shown && (std::exchange(scene.shown_closed, false) || (scene.simulate && scene.info_tab == ui::screens::AttractionTab::Customers && scene.since_info_page >= kLivePageInterval)))
            {
                scene.since_info_page = 0;
                show_info_page(scene, *info, assets, options);
            }
            const auto action = info->take_action();
            if (action && scene.shown_prop && *action < scene.prop_tabs.size() && scene.prop_tabs[*action] == ui::screens::SceneryTab::Destroy)
            {
                // Pulled down at once and paid for, as in the game; the panel goes with it.
                if (delete_props(device, scene, assets, std::span(&*scene.shown_prop, 1)).has_value())
                {
                    scene.shown_prop.reset();
                    info->hide();
                }
            }
            // A changing room's construction tab opens the pool panel, to build on at its complex.
            if (action && scene.shown_prop && *action < scene.prop_tabs.size() && scene.prop_tabs[*action] == ui::screens::SceneryTab::RoomConstruction)
                if (const auto pools = scene.desktop->root().find_as<ui::screens::PoolPanel>("pool_panel"); !pools || !pools->showing())
                    toggle_pool_panel(device, scene, assets, info_units(assets, options).currency);
            if (action && scene.shown_person && *action < scene.person_tabs.size() && scene.person_tabs[*action] == ui::screens::PersonTab::Party)
                open_party(scene, *info, *scene.shown_person, assets, options);
            if (action && scene.shown && *action < scene.shown_tabs.size() && scene.shown_tabs[*action] == ui::screens::AttractionTab::Construction)
                scene.construct_requested = *scene.shown;
            const auto door = [](const ui::screens::InfoChange& c) { return c.id == ui::screens::control_id(ui::screens::AttractionControl::Entrance) || c.id == ui::screens::control_id(ui::screens::AttractionControl::Exit); };
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
            if (const auto change = info->take_change(); change && scene.shown && scene.shown->kind == rct3::park::AttractionRef::Kind::Ride && door(*change))
            {
                if (start_door(scene.shown->index, change->id == ui::screens::control_id(ui::screens::AttractionControl::Exit)))
                {
                    stop_land_tools(scene);
                    info->hide();
                }
            }
            else if (change && scene.shown && change->id == ui::screens::control_id(ui::screens::AttractionControl::Demolish))
                ask_demolish(scene, *assets.strings, assets.palette, { *scene.shown });
            // A pool ride's join button opens the pool panel on its paths page, the window staying, as in the game.
            else if (change && scene.shown && change->id == ui::screens::control_id(ui::screens::AttractionControl::JoinPool))
                toggle_pool_panel(device, scene, assets, info_units(assets, options).currency, ui::screens::PoolPage::Paths);
            else if (change && scene.shown)
            {
                change_attraction(device, scene, *info, *scene.shown, *change, assets, options);
                // Longer trains fit fewer times round the track, so the page shows the new limits; a new name and
                // the graph's series show.
                if (change->id == ui::screens::control_id(ui::screens::AttractionControl::Trains) || change->id == ui::screens::control_id(ui::screens::AttractionControl::Cars) || change->id == ui::screens::control_id(ui::screens::AttractionControl::Name) || change->id == ui::screens::control_id(ui::screens::AttractionControl::CarKind)
                    || (change->id >= ui::screens::control_id(ui::screens::AttractionControl::SpeedSeries) && change->id <= ui::screens::control_id(ui::screens::AttractionControl::LateralSeries)))
                    show_info_page(scene, *info, assets, options);
            }
            else if (const auto room = change && scene.shown_prop ? pool_room(scene, scene.objects[*scene.shown_prop]) : std::nullopt)
            {
                const save::model::PlacedObject& o = scene.objects[*scene.shown_prop];
                if (change->id == ui::screens::kPoolDeleteRoomControl)
                {
                    scene.pools.rooms.erase(scene.pools.rooms.begin() + static_cast<std::ptrdiff_t>(*room));
                    pools_changed(device, scene, assets);
                    scene.shown_prop.reset();
                    info->hide();
                }
                else if (change->id == ui::screens::kPoolDeleteComplexControl)
                    ask_sell_pool_complex(scene, *assets.strings, assets.palette, *room);
                else if (change->id == ui::screens::kPoolMechanicControl)
                {
                    if (const entt::entity e = room_entity(scene, *room); e != entt::null)
                        scene.sim.registry.get_or_emplace<simulation::Upkeep>(e).called = true;
                }
                else if (change->id == ui::screens::kPoolInspectionControl)
                {
                    // Each room is inspected on its own, as each ride is.
                    if (const entt::entity e = room_entity(scene, *room); e != entt::null && change->value >= 0 && std::cmp_less(change->value, simulation::kInspectionSeconds.size()))
                        scene.sim.registry.get_or_emplace<simulation::Upkeep>(e).inspect_every = { simulation::kInspectionSeconds[static_cast<std::size_t>(change->value)] };
                }
                else if (change->id == ui::screens::kPoolLocateControl)
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
                        if (change->id == ui::screens::kPoolTicketControl)
                            scene.pools.rooms[r].ticket = change->value;
                        else if (change->id == ui::screens::kPoolFluidControl)
                            scene.pools.rooms[r].fluid = static_cast<std::uint8_t>(std::clamp(change->value, 0, 2));
                        else if (change->id == ui::screens::kPoolWaveHeightControl)
                            scene.pools.rooms[r].waves.height = static_cast<float>(change->value);
                        else if (change->id == ui::screens::kPoolWaveLengthControl)
                            scene.pools.rooms[r].waves.frequency = rct3::park::pools::kLeastWaveFrequency + rct3::park::pools::kMostWaveFrequency - static_cast<float>(change->value);
                        else if (change->id == ui::screens::kPoolWaveOnControl)
                            scene.pools.rooms[r].waves.on = static_cast<float>(change->value);
                        else if (change->id == ui::screens::kPoolWaveEveryControl)
                            scene.pools.rooms[r].waves.every = static_cast<float>(change->value) * 60;
                        else if (change->id == ui::screens::kPoolNameControl && change->text.find_first_not_of(u' ') != std::u16string::npos)
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
                if (change->id == ui::screens::person_control(ui::screens::PersonControl::PickUp))
                {
                    // Only those walking in the park; riders are left be.
                    if (!pick_up_person(scene, *scene.shown_person) && (scene.shown_people[*scene.shown_person].guest == entt::null || scene.sim.registry.all_of<simulation::Left>(scene.shown_people[*scene.shown_person].guest)))
                        simulation::park::post(scene.park.messages, { .text_key = "Pincer_NotOwned" });
                }
                else if (change->id == ui::screens::person_control(ui::screens::PersonControl::Sack))
                {
                    fire(scene, *scene.shown_person);
                    scene.shown_person.reset();
                    info->hide();
                }
                else if (change_person(scene, *scene.shown_person, *change))
                {
                    show_info_page(scene, *info, assets, options);
                    if (change->id == ui::screens::person_control(ui::screens::PersonControl::Name))
                        name_cheats(scene, assets, *scene.shown_person, change->text);
                }
            }
            const auto paint = info->take_paint();
            if (paint && scene.shown_person)
                paint_person(scene, *scene.shown_person, paint->pot, paint->colour);
            if (paint && scene.shown_prop)
            {
                // Pot k paints the k-th slot the prop takes; free, as in the game.
                const std::vector<std::size_t> slots = ui::screens::scenery_paint_slots(prop_facts(scene, *scene.shown_prop));
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
                if (*tab < ui::screens::kPartyTabs.size())
                {
                    scene.party_tab = ui::screens::kPartyTabs[*tab].tab;
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
                const std::vector<ui::screens::StateChoice> states = ui::screens::state_choices(ui::screens::AttractionKind::Stall);
                if (choice->second < states.size())
                    for (const std::size_t r : rct3::park::pools::pool_complex_rooms(scene.pools, *room))
                        scene.pools.rooms[r].open = states[choice->second].state == save::model::AttractionState::Open;
                open_prop(scene, *info, *scene.shown_prop, assets, options, scene.prop_tab);
            }
            if (choice && scene.shown)
            {
                // Put in the state picked, then shown again for the state tab's new look.
                const ui::screens::AttractionFacts f = facts(scene, *scene.shown);
                const std::vector<ui::screens::StateChoice> states = state_choices(scene, *scene.shown);
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
        if (const auto asked = std::exchange(scene.rename_requested, std::nullopt); asked && change_person(scene, asked->first, ui::screens::InfoChange { .id = ui::screens::person_control(ui::screens::PersonControl::Name), .text = asked->second }))
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
            else if (const auto overlay = ride_track_overlay(scene, asked->index))
            {
                if (open_track_building(scene, asked->index, *overlay, assets))
                {
                    close_footpath_panel(scene); // One tool panel at the bottom left at a time
                    stop_land_tools(scene);
                    stop_deleting(scene);
                }
            }
        }
        profiling::section("TrackBuilding", [&] {
            const ui::Rect screen = desktop.root().rect();
            update_track_building(device, scene, assets, options.choice(config::GameOption::Currency), info_units(assets, options).distance, pointer.position, { screen.width(), screen.height() }, over, input.shift, input.commands, elapsed);
        });
        // A slide built up to a pool, or taken away, opens or closes its railing there.
        if (!scene.pools.squares.empty() && pool_openings(scene) != scene.pool_openings)
            pools_changed(device, scene, assets);
        scene.view_camera.yaw += kViewTurn * elapsed;
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
        scene.clock += elapsed;
        const float park_seconds = elapsed * simulation::time_factor(scene.park.speed);
        scene.park_clock += park_seconds;
        scene.chain_shifts.resize(scene.rides.rides.size());
        for (std::size_t i = 0; i < scene.chain_shifts.size(); ++i)
            scene.chain_shifts[i] = rct3::park::chain_shift(scene.chain_shifts[i], scene.rides.rides[i].options.lift_speed, park_seconds);
        advance_park_particles(scene.particles, park_seconds, viewer, static_cast<std::size_t>(options.get(config::GameOption::ParticleBudget)));
        scene.water.look = water_look(options);
        scene.shadows = options.choice(config::GameOption::Shadows);
        scene.blend_shadows = options.flag(config::GameOption::BlendShadows);
        scene.blend_band = options.get(config::GameOption::DetailBlendBand);
        scene.level_scales = level_scales(options);
        scene.bloom = options.flag(config::GameOption::Bloom);
        scene.light_shafts = options.flag(config::GameOption::LightShafts);
        scene.ambient_occlusion = options.flag(config::GameOption::AmbientOcclusion);
        scene.surface_detail = options.choice(config::GameOption::SurfaceDetail);
        advance_park_water(scene.water, park_seconds);
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
            advance_park_lens(scene.lens, around, options.flag(config::GameOption::WaterDroplets) && options.flag(config::GameOption::Refraction), elapsed);
        });
        profiling::section("People", [&] { refresh_people(scene, viewer, rct3::park::view_projection(scene.camera, aspect(device), scene.seat, scene.seat_across), elapsed); });
        profiling::section("ZooAnimals", [&] { place_animals(scene); });
        auto bar = desktop.root().find_as<ui::screens::ParkBar>(kBarName);
        if (bar)
        {
            if (const auto asked = bar->take_speed())
                scene.park.speed = *asked;
            if (const auto mode = bar->take_day_mode())
                simulation::change_mode(scene.day, *mode);
            if (bar->objective_button().take_click())
                open_management_window(desktop, ui::screens::ManagementTopic::Objectives, 0, management_facts(scene), *assets.strings, assets.palette, info_units(assets, options));
            if (bar->take_finances())
                open_management_window(desktop, ui::screens::ManagementTopic::Finances, 0, management_facts(scene), *assets.strings, assets.palette, info_units(assets, options));
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
                ride_action(scene, assets, ui::screens::RideCameraAction::NextCamera);
            else if (c == ui::PlayCommand::PreviousRideCamera)
                ride_action(scene, assets, ui::screens::RideCameraAction::PreviousCamera);
            else if (c == ui::PlayCommand::NextRide)
                ride_action(scene, assets, ui::screens::RideCameraAction::NextTrain);
            else if (c == ui::PlayCommand::PreviousRide)
                ride_action(scene, assets, ui::screens::RideCameraAction::PreviousTrain);
            else if (c == ui::PlayCommand::NextCoasterCamType)
                ride_action(scene, assets, ui::screens::RideCameraAction::ChangeType);
        }
        profiling::section("Simulation", [&] {
            // The park's day, its calendar and its messages' ages run at the speed picked, and stop while paused.
            const float game_seconds = elapsed * simulation::time_factor(scene.park.speed);
            simulation::advance(scene.day, game_seconds);
            const float before = scene.park.time;
            const std::int32_t weeks = simulation::park::pass_time(scene.park, game_seconds);
            simulation::advance_weather(scene.park, scene.park.time - before, scene.gameplay, options.flag(config::GameOption::DisableWeather));
            scene.thunder = rct3::park::advance_weather_look(scene.weather_look, scene.park.weather, scene.park.speed.paused ? 0.0f : elapsed, scene.weather_dice);
            // Each new week rates the park again; weeks run through in one frame are rated once.
            if (weeks > 0)
            {
                // While the park runs, its own litter counts, and much of it is told to the player.
                const std::optional<std::int32_t> old = scene.simulate ? std::optional(simulation::old_litter(scene.sim)) : std::nullopt;
                profiling::section("Rating", [&] { simulation::park::rate_week(scene.park, scene.people, scene.rides, old); });
                if (old >= kLitterWarning)
                    simulation::park::post(scene.park.messages, { .text_key = "Thought_PI_Park_Litter1" });
                // Last month's takings, for the scenario's goals on money.
                std::int64_t rides_taken = 0;
                std::int64_t shops_taken = 0;
                for (const auto [e, attraction] : scene.sim.registry.view<const simulation::Attraction>().each())
                    (attraction.kind == simulation::AttractionKind::Ride ? rides_taken : shops_taken) += attraction.income.value;
                simulation::count_takings(scene.takings, scene.park.time - scene.park.zero_time, rides_taken, shops_taken);
                {
                    // The week's figures for the park's graphs.
                    std::array<std::int32_t, 3> ages { };
                    for (const auto [e, visitor] : scene.sim.registry.view<const simulation::Visitor>(entt::exclude<simulation::Left>).each())
                        ++ages[static_cast<std::size_t>(visitor.age)];
                    std::int64_t intake = 0;
                    if (const auto* gate = scene.sim.registry.ctx().find<simulation::Turnstile>())
                        for (const std::uint32_t n : gate->in)
                            intake += n;
                    // Where money never runs out the park books nothing, so its income stays flat.
                    const std::int64_t income = scene.park.cash ? rides_taken + shops_taken : 0;
                    const std::int64_t cash = scene.park.cash.value_or(0);
                    // shortcut: the week the park was opened in counts nothing in, taken or made; keep the week's totals
                    // in the park file to count it whole.
                    auto& began = scene.week_began;
                    if (!began.set)
                        began = { .intake = intake, .income = income, .cash = cash, .set = true };
                    simulation::park::close_week(
                        scene.park_model,
                        {
                            .rating = static_cast<std::int32_t>(std::lround(scene.park.rating.value_or(0) * simulation::park::kMaxRating)),
                            .guests = scene.park.guests,
                            .adults = ages[static_cast<std::size_t>(simulation::AgeGroup::Adult)],
                            .teenagers = ages[static_cast<std::size_t>(simulation::AgeGroup::Teen)],
                            .children = ages[static_cast<std::size_t>(simulation::AgeGroup::Child)],
                            .intake = static_cast<std::int32_t>(intake - began.intake),
                            .cash_less_loan = cash - scene.park.loan,
                            .income = income - began.income,
                            .profit = cash - began.cash,
                        });
                    began = { .intake = intake, .income = income, .cash = cash, .set = true };
                }
                // Awards, as each month begins, from the park as it runs. The month the park opens in is not judged.
                const bool new_month = scene.awards_month >= 0 && scene.park.date.month != scene.awards_month;
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
                    // shortcut: rides do not crash here, so only crashes the save remembers count; count new ones once
                    // trains can crash.
                    f.crashed = static_cast<std::int32_t>(std::ranges::count_if(scene.rides.rides, [](const save::model::Ride& ride) { return ride.base.time_since_crash >= 0; }));
                    f.coaster_dangers = scene.park_model.awards.dangerous_coaster_events.value_or(0);
                    f.pool_dangers = scene.park_model.awards.dangerous_pool_events.value_or(0);
                    if (scene.build.items.valid() && scene.build.items.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                    {
                        f.themed = simulation::themed_attractions(scene.rides, scene.objects, scene.build.items.get());
                        judge_themes(scene);
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
                        if (scene.park.cash && slot < scene.park_model.awards.prize_money.size())
                            *scene.park.cash += scene.park_model.awards.prize_money[slot];
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
                // shortcut: the park's value stays as saved, as the simulation does not value the park yet; work it out once it does.
                const simulation::ParkFigures figures {
                    .park_value = std::llround(scene.park_model.finance.park_value.latest().value_or(0)),
                    .guests = scene.park.guests,
                    .rating = static_cast<std::int32_t>(std::lround(scene.park.rating.value_or(0) * simulation::park::kMaxRating)),
                    .loan = scene.park.loan,
                    .ride_income = scene.takings.rides,
                    .shop_profit = scene.takings.shops,
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
                        simulation::give_prize(scene.park, scene.park_model.finance, scene.grid, prize);
                        scene.prizes.emplace_back(simulation::prize_message(prize));
                        // A level won, every guest out on the paths jumps for joy.
                        if (prize >= simulation::kApprenticePrize)
                        {
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
                    constexpr std::array<std::string_view, 25> kKeys { // By Finding
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
                            message.panel = ui::screens::message_panel(finding == TooDear || finding == TooCheap ? ui::screens::AttractionTab::Items : (finding == NoMechanic || finding == NoMechanicCanReach || finding == BadZones) && !stall ? ui::screens::AttractionTab::Maintenance : ui::screens::AttractionTab::Mode);
                        }
                        // shortcut: a worker is named but cannot be gone to from the message; add it once messages open people.
                        else if (const auto shown = std::ranges::find(scene.shown_people, at, &ShownPerson::guest); r.valid(at) && shown != scene.shown_people.end() && shown->staff && shown->saved < scene.people.staff.size())
                        {
                            message.subject_name = strings::to_utf8(ui::screens::person_name({ .staff = &scene.people.staff[shown->saved] }, *assets.strings));
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
                            if (auto panel = scene.desktop->root().find_as<ui::screens::InfoPanel>(kInfoName); panel && scene.shown_person == i)
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
                            if (auto panel = scene.desktop->root().find_as<ui::screens::InfoPanel>(kInfoName); panel && scene.shown_person == i)
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
                        const simulation::Radians facing { std::atan2(-m[2][0], -m[2][2]) };
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
                        scene.photos.push_back({ rct3::park::framed(points, yaw, kPhotoPitch, kPhotoClosest), std::format("{} at photo spot_", name.empty() ? "Guest" : name) });
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
                        const float newest = std::ranges::max(slots, { }, &save::model::Thought::time).time;
                        const auto* about = scene.sim.registry.valid(t.about) ? scene.sim.registry.try_get<simulation::Attraction>(t.about) : nullptr;
                        std::shift_right(slots.begin(), slots.end(), 1);
                        // A thought about a path item is about the placed object it stands as.
                        const auto* item = about == nullptr && scene.sim.registry.valid(t.about) ? scene.sim.registry.try_get<simulation::PathItem>(t.about) : nullptr;
                        slots[0] = { .about = { about != nullptr ? about->saved : item != nullptr ? item->object : 0 }, .id = std::to_underlying(t.kind), .time = newest + 1, .variant = static_cast<std::int32_t>(t.variant) };
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
                    if (scene.park.cash)
                    {
                        *scene.park.cash -= payroll->paid.value - scene.wages_seen;
                        simulation::park::ledger_month(scene.park_model.finance, scene.ledger_month, scene.park.date).staff_wages += static_cast<std::int32_t>(payroll->paid.value - scene.wages_seen);
                    }
                    scene.wages_seen = payroll->paid.value;
                }
                // So are pools' cleaning fluids and stalls' stock.
                if (const auto* supplies = scene.sim.registry.ctx().find<simulation::Supplies>())
                {
                    if (scene.park.cash)
                        *scene.park.cash -= supplies->paid.value - scene.supplies_seen;
                    scene.supplies_seen = supplies->paid.value;
                }
                // What guests pay for rides, shops and entry goes into it.
                if (const auto* receipts = scene.sim.registry.ctx().find<simulation::Receipts>())
                {
                    if (scene.park.cash)
                        *scene.park.cash += receipts->paid.value - scene.receipts_seen;
                    scene.receipts_seen = receipts->paid.value;
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
                    ride.base.time_since_inspection = upkeep->since_inspection.value;
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
                    ride.base.time_since_crash = wrecks->since.value;
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
                for (const auto [e, upkeep, attraction] : scene.sim.registry.view<const simulation::Upkeep, const simulation::Attraction>().each())
                {
                    const auto at = std::ranges::find(scene.ride_entities, e);
                    const auto index = static_cast<std::size_t>(at - scene.ride_entities.begin());
                    // Stalls and cash machines break with time and have no maintenance page: their messages open
                    // the window, and the window shows the state.
                    const bool stall = upkeep.break_in.value >= 0;
                    const auto tell = [&](std::string_view key) {
                        if (stall)
                            simulation::park::post(scene.park.messages, { .text_key = std::string(key), .subject = attraction.saved != 0 ? std::optional(attraction.saved) : std::nullopt, .panel = ui::screens::message_panel(ui::screens::AttractionTab::View) }); // Named once shown, as the window names it
                        else
                            simulation::park::post(scene.park.messages, { .text_key = std::string(key), .subject = index < scene.rides.rides.size() ? std::optional(scene.rides.rides[index].base.id) : std::nullopt, .subject_name = attraction.name, .panel = ui::screens::message_panel(ui::screens::AttractionTab::Maintenance), .field_key = std::string(ui::screens::breakdown_key(static_cast<save::model::BreakdownKind>(std::to_underlying(upkeep.kind)))) });
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
                    if (auto panel = scene.desktop->root().find_as<ui::screens::InfoPanel>(kInfoName); panel && panel->visible && scene.shown && scene.shown == attraction_with(scene, attraction.saved))
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
            ui::screens::ParkBarUnits units;
            units.currency = ui::currency(*assets.strings, options.choice(config::GameOption::Currency));
            units.fahrenheit = options.choice(config::GameOption::TemperatureUnits) == kFahrenheit;
            // The game writes its times with am and pm in English only.
            units.twelve_hour = assets.fonts.language == ui::kDefaultLanguage;
            ui::screens::ParkBarValues values = ui::screens::park_bar_values(scene.park, scene.day.time, *assets.strings, units);
            // The player chooses how the day goes in a sandbox, whatever its money.
            if (scene.game.sandbox)
                values.day_mode = scene.day.mode;
            bar->show(values);
        }
        if (auto messages = desktop.root().find_as<ui::screens::MessageDrawer>(kMessagesName))
        {
            // shortcut: only rides and stalls can be gone to, not people or path items; add them when a message names one.
            // Saved parks keep messages without their subject's name, so it is filled in from the attraction.
            for (simulation::park::ParkMessage& m : scene.park.messages.list)
            {
                const auto ref = m.subject && m.subject_name.empty() ? attraction_with(scene, *m.subject) : std::nullopt;
                if (ref)
                    m.subject_name = strings::to_utf8(ui::screens::attraction_name(ref->kind == rct3::park::AttractionRef::Kind::Stall ? scene.rides.stalls[ref->index].base : scene.rides.rides[ref->index].base, *assets.strings));
            }
            std::vector<ui::screens::DrawerMessage> shown = ui::screens::drawer_messages(scene.park.messages, *assets.strings, messages->style());
            // A message about an attraction shows the attraction's own icon, the one it has in the build lists, and
            // the camera when it has none.
            const auto items = scene.build.items.valid() && scene.build.items.wait_for(std::chrono::seconds(0)) == std::future_status::ready ? std::span(scene.build.items.get()) : std::span<const rct3::park::BuildItem> { };
            for (std::size_t i = 0; i < shown.size(); ++i)
            {
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
                open_management_window(desktop, ui::screens::ManagementTopic::ParkOperations, ui::screens::kAwardsTab, management_facts(scene), *assets.strings, assets.palette, info_units(assets, options));
            // An attraction's takes the view to it and opens the info panel on it.
            else if (const auto ref = message != scene.park.messages.list.end() && message->subject ? attraction_with(scene, *message->subject) : std::nullopt; ref && info)
            {
                // At the tab the message names; one the attraction lacks opens its view.
                open_info(scene, *info, *ref, assets, options, ui::screens::message_tab(message->panel.value_or(-1)));
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
        if (auto tools = desktop.root().find_as<ui::screens::ToolRow>(kToolsName))
        {
            const std::optional<ui::screens::ParkTool> pressed = tools->take_pressed();
            if (pressed == ui::screens::ParkTool::Leave)
                ask_leave(scene, *assets.strings, assets.palette);
            else if (pressed == ui::screens::ParkTool::Map)
                toggle_park_map(scene.map, desktop, *assets.strings, assets.palette);
            else if (pressed == ui::screens::ParkTool::Paths)
            {
                close_track_building(scene); // One tool panel at the bottom left at a time
                stop_ride_placing(scene);
                stop_land_tools(scene);
                stop_deleting(scene);
                stop_structure_tools(scene);
                stop_pooling(scene);
                toggle_footpath_panel(device, scene, assets, options);
            }
            if (const auto item = tools->take_item())
            {
                if (*item == ui::screens::ToolItem::ShapeGround || *item == ui::screens::ToolItem::PaintGround || *item == ui::screens::ToolItem::Water || *item == ui::screens::ToolItem::Waterfalls)
                {
                    close_track_building(scene);
                    stop_ride_placing(scene);
                    close_footpath_panel(scene);
                    stop_deleting(scene);
                    stop_structure_tools(scene);
                    stop_pooling(scene);
                    using enum LandTools::Tool;
                    toggle_land_tool(scene, *item == ui::screens::ToolItem::ShapeGround ? Shape : (*item == ui::screens::ToolItem::PaintGround ? Paint : (*item == ui::screens::ToolItem::Water ? Water : Waterfall)), assets);
                }
                else if (*item == ui::screens::ToolItem::DeleteOne || *item == ui::screens::ToolItem::DeleteArea)
                {
                    close_track_building(scene);
                    stop_ride_placing(scene);
                    close_footpath_panel(scene);
                    stop_land_tools(scene);
                    stop_structure_tools(scene);
                    stop_pooling(scene);
                    const auto tool = *item == ui::screens::ToolItem::DeleteOne ? DeleteTools::Tool::One : DeleteTools::Tool::Area;
                    const bool out = scene.deleting.tool == tool;
                    stop_deleting(scene);
                    if (!out)
                        scene.deleting.tool = tool;
                    if (!out && tool == DeleteTools::Tool::Area)
                        desktop.root().add<ui::screens::DeleteAreaPanel>(std::string(kDeleteAreaName), skins::delete_area_panel_style(), *assets.strings);
                }
                else if (*item == ui::screens::ToolItem::StructureGrab || *item == ui::screens::ToolItem::StructureList)
                {
                    close_track_building(scene);
                    stop_ride_placing(scene);
                    close_footpath_panel(scene);
                    stop_land_tools(scene);
                    stop_deleting(scene);
                    stop_pooling(scene);
                    toggle_structure_tool(scene, *item == ui::screens::ToolItem::StructureGrab ? StructureTools::Tool::Grab : StructureTools::Tool::Place, assets);
                }
                else if (*item == ui::screens::ToolItem::Pools)
                {
                    // The pools button opens the pool panel, as in the game, not a list.
                    close_track_building(scene);
                    stop_ride_placing(scene);
                    close_footpath_panel(scene);
                    stop_land_tools(scene);
                    stop_deleting(scene);
                    stop_structure_tools(scene);
                    stop_enclosing(scene);
                    close_build_list(desktop.root());
                    toggle_pool_panel(device, scene, assets, ui::currency(*assets.strings, options.choice(config::GameOption::Currency)));
                }
                else if (*item == ui::screens::ToolItem::People)
                    scene.show_people = tools->switched_on(*item);
                else if (*item == ui::screens::ToolItem::Scenery)
                    scene.show_scenery = tools->switched_on(*item);
                else if (*item == ui::screens::ToolItem::SeeThroughGround)
                    scene.see_through_ground = !tools->switched_on(*item); // Switched on, the ground shows whole
                else if (*item == ui::screens::ToolItem::Grid)
                    scene.show_grid = tools->switched_on(*item);
                else if (*item == ui::screens::ToolItem::Contours)
                    scene.show_contours = tools->switched_on(*item);
                else if (*item == ui::screens::ToolItem::PathHeights)
                    scene.show_path_heights = tools->switched_on(*item);
                else if (*item == ui::screens::ToolItem::RideHeights)
                    scene.show_ride_heights = tools->switched_on(*item);
                else if (*item == ui::screens::ToolItem::Money)
                    scene.show_money = tools->switched_on(*item);
                else if (*item == ui::screens::ToolItem::GuestPayments)
                    scene.show_guest_payments = tools->switched_on(*item);
                else if (*item == ui::screens::ToolItem::Supports)
                {
                    scene.show_supports = tools->switched_on(*item);
                    scene.levels = { };
                    scene.since_rebuild = kObjectRebuildInterval;
                }
                else if (*item == ui::screens::ToolItem::Options || *item == ui::screens::ToolItem::About || *item == ui::screens::ToolItem::Save || *item == ui::screens::ToolItem::Load)
                    scene.window_requests.push_back(*item);
                else if (const auto topic = ui::screens::management_topic(*item))
                    toggle_management_window(desktop, *topic, management_facts(scene), *assets.strings, assets.palette, info_units(assets, options));
                else
                    open_build_list(scene.build, desktop.root(), *item, tools->item(*item), *tools, *assets.strings, assets.palette, ui::currency(*assets.strings, options.choice(config::GameOption::Currency)));
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
                if (const auto name = config::game_saves_folder(config::UserFolder::Coasters); scene.saves.game_saves && name)
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
                    close_track_building(scene);
                    close_footpath_panel(scene);
                    stop_land_tools(scene);
                    stop_deleting(scene);
                    stop_structure_tools(scene);
                }
                else if (start_enclosing(scene, *picked))
                {
                    stop_pooling(scene);
                    stop_ride_placing(scene);
                    close_track_building(scene);
                    close_footpath_panel(scene);
                    stop_land_tools(scene);
                    stop_deleting(scene);
                    stop_structure_tools(scene);
                }
                else if (start_ride_placing(scene, *picked))
                {
                    stop_enclosing(scene);
                    stop_pooling(scene);
                    close_track_building(scene);
                    close_footpath_panel(scene);
                    stop_land_tools(scene);
                    stop_deleting(scene);
                    stop_structure_tools(scene);
                    building = picked->kind != rct3::park::BuildKind::Scenery;
                }
                else if (new_tracked_ride(scene, *picked, assets))
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
                    if (auto tools = desktop.root().find_as<ui::screens::ToolRow>(kToolsName); tools && tools->open_menu() && (*tools->open_menu() == ui::screens::ParkTool::Rides || *tools->open_menu() == ui::screens::ParkTool::Shops || *tools->open_menu() == ui::screens::ParkTool::Scenery || *tools->open_menu() == ui::screens::ParkTool::Paths))
                        tools->close_menu();
                }
            }
        }
        update_view_lines(device, scene);
        if (auto heights = desktop.root().find_as<ui::widgets::WorldLabels>(kHeightsName))
        {
            const ui::Rect view = desktop.root().rect();
            heights->labels = height_labels(scene, { view.width(), view.height() });
            // While the park bar's highlights are on, the people the player picked have their name and what
            // they are doing floating over them.
            if (const auto bar = desktop.root().find_as<ui::screens::ParkBar>(kBarName); bar && bar->highlights() && view.width() > 0 && view.height() > 0)
            {
                constexpr float kHighlightLift = 2.4f; ///< Chosen by eye: metres over their feet, clear of their head
                const glm::mat4 camera = rct3::park::view_projection(scene.camera, view.width() / view.height());
                for (const ShownPerson& p : scene.shown_people)
                {
                    const ui::screens::PersonFacts f { .guest = p.staff ? nullptr : &scene.people.guests[p.saved], .staff = p.staff ? &scene.people.staff[p.saved] : nullptr, .people = &scene.people, .rides = &scene.rides };
                    if (!f.person()->highlight)
                        continue;
                    const glm::vec4 clip = camera * (p.pose.world * glm::vec4(0, kHighlightLift, 0, 1));
                    const glm::vec2 ndc = glm::vec2(clip) / clip.w;
                    if (clip.w > 0 && std::abs(ndc.x) <= 1 && std::abs(ndc.y) <= 1)
                        heights->labels.push_back({ { (ndc.x + 1) / 2 * view.width(), (1 - ndc.y) / 2 * view.height() }, ui::screens::person_name(f, *assets.strings) + u"\n" + ui::screens::person_doing(f, *assets.strings) });
                }
            }
        }
        if (auto money = desktop.root().find_as<ui::widgets::MoneyFloaters>(kMoneyName))
        {
            const ui::Rect view = desktop.root().rect();
            money->floats = money_floats(scene, { view.width(), view.height() }, ui::currency(*assets.strings, options.choice(config::GameOption::Currency)), elapsed);
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
            ui::screens::StaffLook& look = scene.staff_looks[p.saved];
            look.colours = { p.pose.look.colours.at(ui::screens::kUpperPalette), p.pose.look.colours.at(ui::screens::kUpperPalette + 1) };
            if (const auto* patrol = scene.sim.registry.valid(p.guest) ? scene.sim.registry.try_get<simulation::Patrol>(p.guest) : nullptr)
                look.patrol_tiles = patrol->tiles.size();
            look.painting_patrol = p.guest != entt::null && p.guest == shown_staff(scene) && scene.painting_patrol;
        }
        const ui::screens::ManagementFacts facts = management_facts(scene);
        const ManagementAsks asks = update_management_windows(desktop, facts, *assets.strings, info_units(assets, options));
        if (asks.row && info)
            open_row(scene, *info, *asks.row, assets, options);
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
        if (asks.staff.change && asks.staff.change->second.id == ui::screens::person_control(ui::screens::PersonControl::PatrolPaint))
            scene.person_tab = ui::screens::PersonTab::General;
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
            if (change->second.id == ui::screens::person_control(ui::screens::PersonControl::Sack))
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
                if (change->second.id != ui::screens::person_control(ui::screens::PersonControl::PatrolPaint))
                    scene.shown_person = kept;
                if (changed && info && scene.shown_person == who)
                    show_info_page(scene, *info, assets, options);
            }
        }
        // Each listed ride that may take the state takes it; the others stay as they are.
        for (const ui::screens::ManagementRow& r : asks.listed)
        {
            if (r.kind == ui::screens::ManagementRow::Kind::Ride && r.index < scene.rides.rides.size())
                (void)change_ride_state(scene, r.index, *asks.state_all);
            else if (r.kind == ui::screens::ManagementRow::Kind::Stall && r.index < scene.rides.stalls.size())
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
        // Every broken path item back new at once, at the full price of each: the dear way, a handyman's the free one.
        if (asks.replace_broken && facts.broken_items > 0 && (!scene.park.cash || *scene.park.cash >= facts.replace_cost))
        {
            simulation::replace_broken(scene.sim);
            if (scene.park.cash)
                *scene.park.cash -= facts.replace_cost;
        }
        profiling::section("Footpaths", [&] { update_footpaths(device, scene, assets, options); });
        profiling::section("Supports", [&] { refresh_supports(scene, assets); });
        follow_footpaths(scene, elapsed);
        if (auto guide = desktop.root().find_as<ui::screens::TutorialGuide>(kGuideName); guide && guide->take_quit())
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
                    if (auto info = desktop.root().find_as<ui::screens::InfoPanel>(kInfoName); info && scene.shown_prop)
                    {
                        scene.shown_prop.reset();
                        info->hide();
                    }
                }
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

    rct3::park::object::LevelScales level_scales(const config::GameOptions& options)
    {
        static_assert(config::kDetailPairs.size() == rct3::park::kDetailKinds); // In the same order
        rct3::park::object::LevelScales scales;
        for (std::size_t i = 0; i < scales.size(); ++i)
            scales[i] = { .detail = options.get(config::kDetailPairs[i].detail), .distance = options.get(config::kDetailPairs[i].distance) };
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
        if (auto paths = scene.desktop->root().find_as<const ui::screens::FootpathPanel>(kFootpathPanelName); paths && paths->showing() && paths->page() == ui::screens::FootpathPage::Controls)
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
                           .ledger = { scene.park_model.finance.ledger.begin(), scene.park_model.finance.ledger.begin() + static_cast<std::ptrdiff_t>(std::min(scene.park_model.finance.ledger_periods_loaded, save::model::kLedgerPeriods)) } };
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
            saved.goals = parkfile::SavedGoals { .objectives = scene.park_model.objectives, .max_loan = scene.park_model.finance.max_loan, .interest_rate_percent = scene.park_model.finance.interest_rate_percent };
        return parkfile::write_saved_park(saved);
    }

} // namespace rct3::scene::park
