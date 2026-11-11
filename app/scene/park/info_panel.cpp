// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park/info_panel.hpp"

#include "logging/log.hpp"
#include "scene/park/attraction_state.hpp"
#include "scene/park/patrol.hpp"
#include "scene/park/people.hpp"
#include "scene/park/refresh.hpp"
#include "scene/park/ride_camera.hpp"
#include "scene/park/scene.hpp"
#include "scene/park/setup.hpp"
#include "simulation/guests.hpp"
#include "simulation/import_park.hpp"
#include "simulation/parties.hpp"
#include "simulation/rides.hpp"
#include "simulation/staff/care.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"
#include "simulation/vip.hpp"
#include "strings/unicode.hpp"
#include "ui/extra_skins.hpp"
#include "ui/game_style.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace rct3::scene::park
{

    namespace
    {

        namespace skins = ui::game_skins;

        constexpr float kPropViewClosest = 1.5f;    ///< Small props fill the round view
        constexpr float kViewPitch = 0.5f;          ///< Radians down, of the info panel's round view
        constexpr float kPersonViewDistance = 4.5f; ///< Of the round view's eye from the person it shows
        constexpr float kPersonViewHeight = 0.9f;   ///< Of the point it looks at, above the person's feet

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
                simulation::Staff pay { .role = simulation::staff::staff_role(f.staff->kind), .training = f.staff->training };
                simulation::staff::set_wage_level(pay, f.staff->wage_level);
                f.wage = pay.wage.value;
                f.training_cost = simulation::staff::training_cost(pay);
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

    } // namespace

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

    ui::screens::InfoUnits info_units(const ui::InterfaceAssets& assets, const config::GameOptions& options)
    {
        ui::screens::InfoUnits units;
        units.currency = ui::currency(*assets.strings, options.choice(config::GameOption::Currency));
        units.distance = static_cast<ui::screens::DistanceUnits>(options.choice(config::GameOption::DistanceUnits));
        return units;
    }

    std::optional<std::size_t> pool_room(const ParkScene& scene, const save::model::PlacedObject& o)
    {
        if (!o.fixture || !std::ranges::contains(scene.pool_pieces, o.id))
            return std::nullopt;
        const auto room = std::ranges::find_if(scene.pools.rooms, [&](const rct3::park::pools::PoolRoom& r) { return r.col == o.col && r.row == o.row && r.symbol == o.symbol; });
        return room == scene.pools.rooms.end() ? std::nullopt : std::optional(static_cast<std::size_t>(room - scene.pools.rooms.begin()));
    }

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

    rct3::park::ParkCamera person_view(const ParkScene& scene, std::size_t shown, float yaw)
    {
        rct3::park::ParkCamera camera;
        camera.target = glm::vec3(scene.shown_people[shown].pose.world[3]) + glm::vec3(0, kPersonViewHeight, 0);
        camera.yaw = yaw;
        camera.pitch = kViewPitch;
        camera.distance = kPersonViewDistance;
        return camera;
    }

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

    void open_info(ParkScene& scene, ui::screens::InfoPanel& panel, rct3::park::AttractionRef ref, const ui::InterfaceAssets& assets, const config::GameOptions& options, std::optional<ui::screens::AttractionTab> tab)
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

    void open_prop(ParkScene& scene, ui::screens::InfoPanel& panel, std::size_t index, const ui::InterfaceAssets& assets, const config::GameOptions& options, std::optional<ui::screens::SceneryTab> open)
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

    void paint_prop(render::Device& device, ParkScene& scene, std::size_t index, std::size_t slot, std::int32_t colour)
    {
        if (slot < rct3::park::kFlexiSlotCount)
            change_object(device, scene, index, [&](save::model::PlacedObject& o) { o.colours.at(slot) = colour; });
    }

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

} // namespace rct3::scene::park
