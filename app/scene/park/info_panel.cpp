// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park/info_panel.hpp"

#include "audio/ride_sounds.hpp"
#include "core/types.hpp"
#include "logging/log.hpp"
#include "park/ride/styles.hpp"
#include "scene/park/attraction_state.hpp"
#include "scene/park/patrol.hpp"
#include "scene/park/people.hpp"
#include "scene/park/refresh.hpp"
#include "scene/park/ride_camera.hpp"
#include "scene/park/scene.hpp"
#include "scene/park/setup.hpp"
#include "scene/ride_placing.hpp"
#include "scene/track/trains.hpp"
#include "simulation/animal_care.hpp"
#include "simulation/animals.hpp"
#include "simulation/crowds.hpp"
#include "simulation/guests.hpp"
#include "simulation/import_park.hpp"
#include "simulation/parties.hpp"
#include "simulation/rides.hpp"
#include "simulation/staff/care.hpp"
#include "simulation/staff/patrol.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"
#include "simulation/vip.hpp"
#include "strings/ascii.hpp"
#include "strings/unicode.hpp"
#include "ui/extra_skins.hpp"
#include "ui/game_style.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <future>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rct3::scene::park
{
    namespace
    {
        /// How many graph samples a ride's first train is from the end of its lap: as far as the last lap was then.
        std::optional<usize> lap_to_go(const simulation::Trains* fleet)
        {
            if (fleet == nullptr || fleet->lap.speed.empty() || fleet->lap.speed.size() > fleet->lap_samples)
                return std::nullopt;
            return fleet->lap_samples - fleet->lap.speed.size();
        }

        namespace skins = ui::game_skins;

        constexpr f32 kPropViewClosest = 1.5f;        ///< Small props fill the round view
        constexpr units::Radians kViewPitch { 0.5f }; ///< Down, of the info panel's round view
        constexpr f32 kPersonViewDistance = 4.5f;     ///< Of the round view's eye from the person it shows
        constexpr f32 kPersonViewHeight = 0.9f;       ///< Of the point it looks at, above the person's feet

        /// The first of the attraction's objects that takes colours, and the slots it takes.
        std::optional<std::pair<save::model::PlacedObjectIndex, std::vector<usize>>> painted_object(const ParkScene& scene, rct3::park::AttractionRef ref)
        {
            for (usize i = 0; i < scene.objects.size(); ++i)
            {
                const auto owner = scene.attractions.find(scene.objects[i].id);
                if (owner == scene.attractions.end() || owner->second != ref)
                    continue;
                const auto model = scene.models.find(rct3::park::object::model_ref(scene.objects[i]));
                ui::screens::info::SceneryFacts f;
                f.colours = scene.objects[i].colours;
                f.colour_slots = model == scene.models.end() ? 0 : rct3::park::object::colour_slots(model->second);
                if (std::vector<usize> slots = ui::screens::info::scenery_paint_slots(f); !slots.empty())
                    return std::pair { save::model::PlacedObjectIndex { i }, std::move(slots) };
            }
            return std::nullopt;
        }

        /// The kind of stall named by `symbol` (as `<name>:sta`) and `overlay` in the build lists, once they are read.
        const rct3::park::BuildItem* stall_kind(const ParkScene& scene, std::string_view symbol, std::string_view overlay)
        {
            const auto& items = scene.build.items;
            if (!items.valid() || items.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return nullptr;
            // Saves keep overlays with either kind of slash.
            const auto same_overlay = [](std::string_view x, std::string_view y) { return std::ranges::equal(x, y, [](char p, char q) { return strings::ascii_lower(p == '\\' ? '/' : p) == strings::ascii_lower(q == '\\' ? '/' : q); }); };
            const auto kind = std::ranges::find_if(items.get(), [&](const rct3::park::BuildItem& i) { return i.stall && same_overlay(i.overlay, overlay) && strings::equals_ignoring_case(std::format("{}:sta", i.symbol), symbol); });
            return kind == items.get().end() ? nullptr : &*kind;
        }

        /// The kind `stall` sells as: its own, or a custom stall's pick.
        const rct3::park::BuildItem* selling_kind(const ParkScene& scene, const save::model::Stall& stall)
        {
            return stall.sells_as.empty() ? stall_kind(scene, stall.base.symbol, stall.base.overlay_path) : stall_kind(scene, stall.sells_as, stall.sells_as_overlay);
        }

        /// The kinds of stall a custom stall may sell as: those the park offers that sell something, by name.
        // shortcut: in the order of their name keys, not of the names shown; upgrade if the two differ much.
        std::vector<const rct3::park::BuildItem*> stall_kinds(const ParkScene& scene)
        {
            std::vector<const rct3::park::BuildItem*> out;
            const auto& items = scene.build.items;
            if (!items.valid() || items.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return out;
            for (const rct3::park::BuildItem& i : items.get())
                if (i.stall && !i.goods.empty() && scene.build.offer.offers(i))
                    out.push_back(&i);
            std::ranges::stable_sort(out, [](const rct3::park::BuildItem* a, const rct3::park::BuildItem* b) { return std::ranges::lexicographical_compare(a->name_text, b->name_text, { }, strings::ascii_lower, strings::ascii_lower); });
            return out;
        }

        ui::screens::info::PersonFacts person_facts(const ParkScene& scene, usize shown)
        {
            ui::screens::info::PersonFacts f;
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
                f.wage = pay.wage;
                f.training_cost = simulation::staff::training_cost(pay);
            }
            else if (!p.staff && p.saved < scene.people.guests.size())
                f.guest = &scene.people.guests[p.saved];
            if (const entt::entity e = shown_staff(scene); e != entt::null)
            {
                const auto* patrol = scene.sim.registry.try_get<simulation::staff::Patrol>(e);
                f.patrol_tiles = patrol != nullptr ? patrol->tiles.size() : 0;
                f.painting_patrol = scene.painting_patrol;
                f.patrol_brush = scene.patrol_brush;
                f.patrol_zone = patrol != nullptr ? patrol->zone : 0;
                const auto& staff = scene.sim.registry.get<simulation::Staff>(e);
                f.uncovered_paths = simulation::staff::uncovered_paths(scene.sim, staff.role);
                if (staff.role == simulation::StaffRole::Handyman)
                    f.items_mended = staff.mended;
                const auto& r = scene.sim.registry;
                const auto* show = r.try_get<simulation::Show>(e);
                f.activity = ui::screens::info::StaffActivity {
                    .job = staff.job,
                    .ride_broken = staff.ride != entt::null && r.valid(staff.ride) && r.all_of<simulation::Upkeep>(staff.ride) ? std::optional(r.get<simulation::Upkeep>(staff.ride).broken) : std::nullopt,
                    .showing = show != nullptr && !show->moves.empty(),
                };
            }
            const auto& r = scene.sim.registry;
            if (const ShownPerson& p = scene.shown_people[shown]; !p.staff && p.guest != entt::null && r.valid(p.guest) && r.all_of<simulation::Vip>(p.guest))
            {
                const auto& vip = r.get<simulation::Vip>(p.guest);
                const auto& goals = scene.park_model.objectives.conditions;
                const auto goal = std::ranges::find(goals, vip.goal, &save::model::Condition::id);
                const f32 happiness = r.all_of<simulation::Mood>(p.guest) ? r.get<simulation::Mood>(p.guest).happiness.value : 0.0f;
                if (goal != goals.end() && goal->vip)
                    f.vip = { vip.patience.value, simulation::vip_opinion(vip, *goal->vip, happiness) };
            }
            // Their passport as the simulation keeps it, the stamps in the inks their posts print in (0xAARRGGBB).
            if (const ShownPerson& p = scene.shown_people[shown]; !p.staff)
            {
                const auto byte = [](i32 c, u32 shift) { return static_cast<u8>(static_cast<u32>(c) >> shift); };
                if (const auto* passport = p.guest != entt::null && r.valid(p.guest) ? r.try_get<simulation::Passport>(p.guest) : nullptr)
                {
                    f.passport.gift = passport->gift;
                    for (const simulation::Passport::Stamp& s : passport->stamps)
                        f.passport.stamps.push_back({ .colour = { byte(s.colour, 16), byte(s.colour, 8), byte(s.colour, 0), byte(s.colour, 24) }, .x = s.at.x, .y = s.at.y });
                }
                if (const auto* rules = r.ctx().find<simulation::PassportRules>())
                {
                    f.passport.stamps_for_gift = rules->stamps_for_gift;
                    const auto gift = std::ranges::find(scene.carried.items, rules->gift, &rct3::park::CarriedKind::symbol);
                    f.passport.gift_name = gift != scene.carried.items.end() ? std::string_view(gift->name_text) : std::string_view { };
                }
                const auto items = r.view<const simulation::PathItem>();
                f.passport.posts = std::ranges::any_of(items, [&](entt::entity e) { return items.get<const simulation::PathItem>(e).kind == simulation::PathItemKind::PassportPost; });
            }
            return f;
        }

        /// The saved group of shown person `shown`, when the save has them in one.
        std::optional<const save::model::GuestGroup&> saved_group(const ParkScene& scene, usize shown)
        {
            const ShownPerson& p = scene.shown_people[shown];
            if (p.staff || p.saved >= scene.people.guests.size() || !scene.people.guests[p.saved].group)
                return std::nullopt;
            const auto group = std::ranges::find(scene.people.groups, scene.people.guests[p.saved].group.id, &save::model::GuestGroup::id);
            return group != scene.people.groups.end() ? std::optional<const save::model::GuestGroup&>(*group) : std::nullopt;
        }

        /// The party shown person `shown` goes round with, as shown people, its leader first: the simulation's
        /// while it walks them, else the save's group; only them when they are on their own.
        std::vector<usize> party_of(const ParkScene& scene, usize shown)
        {
            std::vector<usize> party;
            const auto add = [&](auto&& matches) {
                const auto at = std::ranges::find_if(scene.shown_people, matches);
                if (at != scene.shown_people.end() && !std::ranges::contains(party, static_cast<usize>(at - scene.shown_people.begin())))
                    party.push_back(static_cast<usize>(at - scene.shown_people.begin()));
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

        ui::screens::info::PartyFacts party_facts(const ParkScene& scene)
        {
            ui::screens::info::PartyFacts f;
            for (const usize shown : scene.shown_party)
                f.members.push_back(person_facts(scene, shown));
            if (const auto group = saved_group(scene, scene.shown_party.front()))
                f.kind = group->group_type;
            return f;
        }

    } // namespace

    ui::screens::info::AttractionFacts facts(const ParkScene& scene, rct3::park::AttractionRef ref)
    {
        ui::screens::info::AttractionFacts f { .guests = scene.people.guests, .refusal = scene.refusal };
        if (ref.kind == rct3::park::AttractionRef::Kind::Stall)
        {
            if (ref.index < scene.rides.stalls.size())
            {
                f.stall = &scene.rides.stalls[ref.index];
                f.common = &f.stall->base;
                if (const rct3::park::BuildItem* kind = selling_kind(scene, *f.stall))
                    for (const rct3::park::BuildItem::Goods& g : kind->goods)
                        f.offers.push_back(std::format("{}:cid", g.item));
                // A custom stall's own kind sells nothing; it sells as another kind the player picks.
                if (const rct3::park::BuildItem* own = stall_kind(scene, f.stall->base.symbol, f.stall->base.overlay_path); own != nullptr && own->goods.empty())
                {
                    const rct3::park::BuildItem* now = f.stall->sells_as.empty() ? nullptr : selling_kind(scene, *f.stall);
                    for (const rct3::park::BuildItem* kind : stall_kinds(scene))
                    {
                        if (kind == now)
                            f.sells_as = f.stall_kinds.size();
                        f.stall_kinds.push_back({ .name_key = kind->name_text, .symbol = kind->symbol });
                    }
                }
                f.carried = scene.carried.items;
                f.carried_extras = scene.carried.extras;
            }
        }
        else if (ref.index < scene.rides.rides.size())
        {
            f.ride = &scene.rides.rides[ref.index];
            f.common = &f.ride->base;
            f.queue_signs = !scene.led_signs.hidden.contains(f.ride->base.id);
            f.seats = flat_view(scene, ref.ride(), FlatEye::Seat, 0) || flat_view(scene, ref.ride(), FlatEye::Fixed, 0);
            f.music = scene.tune_labels;
            f.tune = audio::tune_of(f.ride->music, scene.tunes);
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
                const rct3::park::track::TrackRules no_rules;
                const rct3::park::track::TrackRules& kind_rules = ref.index < scene.track_rules.size() ? scene.track_rules[ref.index] : no_rules;
                f.pool_ride = kind_rules.pool_ride;
                f.rated_without_lap = rct3::park::track::rated_without_lap(kind_rules, f.ride->mode);
                // Its trains can change while it is not open, as in the game, and while none of them has riders
                // aboard (set_trains).
                const entt::entity e = scene.simulate && ref.index < scene.ride_entities.size() ? scene.ride_entities[ref.index] : entt::null;
                const auto* fleet = e != entt::null ? scene.sim.registry.try_get<simulation::Trains>(e) : nullptr;
                f.lap_to_go = lap_to_go(fleet);
                const bool aboard = fleet && std::ranges::any_of(fleet->riders, [](const auto& riders) { return !riders.empty(); });
                const auto limits = scene.train_limits.find(ref.ride());
                if (limits != scene.train_limits.end() && f.ride->base.state != save::model::AttractionState::Open && !aboard)
                {
                    const auto choices = scene.train_choices.find(ref.ride());
                    f.other_cars = choices != scene.train_choices.end() && choices->second.size() > 1;
                    f.trains = limits->second;
                    // A ride from a saved park measured by its cars' kept lengths until its trains are made here.
                    std::vector<rct3::park::object::CarFit> kept;
                    for (const save::model::Car& car : f.ride->trains.empty() ? std::vector<save::model::Car> { } : f.ride->trains.front().cars)
                        kept.push_back({ .length = car.length.value });
                    const auto fits = scene.car_fits.find(ref.ride());
                    // A shuttle runs one (fit_trains).
                    const auto coaster = course ? simulation::coaster_of(*course, *f.ride) : std::nullopt;
                    f.trains->most_trains = course && !(coaster && coaster->shuttle != simulation::Shuttle::None) ? rct3::park::track::trains_fitting(*course, fits != scene.car_fits.end() ? std::span<const rct3::park::object::CarFit>(fits->second) : kept, f.ride->options.block_sectioned, kind_rules.most_trains) : 1;
                }
            }
        }
        // A running ride's seats, queue and riders an hour.
        if (f.ride != nullptr && scene.simulate && ref.index < scene.ride_entities.size() && scene.ride_entities[ref.index] != entt::null)
        {
            if (const auto* cycle = scene.sim.registry.try_get<simulation::CycleRide>(scene.ride_entities[ref.index]))
            {
                f.flow = ui::screens::info::RideFlow { cycle->capacity, cycle->queue.size(), cycle->per_hour, cycle->riders.size() };
                // A tracked ride's riders are aboard its trains.
                if (const auto* fleet = scene.sim.registry.try_get<simulation::Trains>(scene.ride_entities[ref.index]))
                    for (const auto& aboard : fleet->riders)
                        f.flow->riding += aboard.size();
            }
        }
        // What it has taken while the park runs, and how fast.
        if (f.common != nullptr && scene.simulate)
            for (const auto [e, a, rate] : scene.sim.registry.view<const simulation::Attraction, const simulation::TakingsRate>().each())
                if (a.saved != 0 && a.saved == f.common->id)
                    f.takings = ui::screens::info::Takings { .income = a.income, .customers = a.customers, .income_per_hour = units::Money { std::llround(rate.income) }, .customers_per_hour = rate.customers };
        f.now = scene.park.time;
        // While its fence button is down, the looks its fence and doors may take, each theme's named after it.
        if (f.ride != nullptr && scene.ride_styles_shown && f.station.value_or(false) && !f.pool_ride && scene.build.items.valid() && scene.build.items.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            const rct3::park::ride::RideStyles styles = rct3::park::ride::ride_styles(scene.build.items.get());
            const auto pick = [](std::span<const rct3::park::ride::ExtraStyle> list, std::optional<usize> picked) {
                ui::screens::info::StylePick p { .picked = picked };
                for (const rct3::park::ride::ExtraStyle& s : list)
                    p.themes.push_back(strings::to_utf16(s.title.empty() ? s.symbol : s.title));
                return p;
            };
            f.fence_style = pick(styles.fences, rct3::park::ride::style_of(styles.fences, f.ride->fence_symbol, f.ride->fence_overlay));
            // Its doors' looks are those of its first station's.
            const auto door = [&](u64 id, rct3::park::ride::DoorEnd end) {
                const auto o = id == 0 ? scene.objects.end() : std::ranges::find(scene.objects, id, &save::model::PlacedObject::id);
                return pick(styles.doors, o == scene.objects.end() ? std::nullopt : rct3::park::ride::style_of(styles.doors, o->symbol, o->overlay, end));
            };
            const save::model::Station no_station;
            const save::model::Station& first = f.ride->stations.empty() ? no_station : f.ride->stations.front();
            f.entrance_style = door(first.entrance, rct3::park::ride::DoorEnd::Entrance);
            f.exit_style = door(first.exit, rct3::park::ride::DoorEnd::Exit);
        }
        if (const auto painted = painted_object(scene, ref))
        {
            for (const usize slot : painted->second)
                f.colours.push_back(static_cast<usize>(std::max(scene.objects[painted->first.value].colours.at(slot), 0)));
        }
        return f;
    }

    ui::screens::info::InfoUnits info_units(const ui::InterfaceAssets& assets, const config::game::GameOptions& options)
    {
        ui::screens::info::InfoUnits units;
        units.currency = ui::currency(*assets.strings, options.choice(config::game::GameOption::Currency));
        units.distance = static_cast<ui::screens::info::DistanceUnits>(options.choice(config::game::GameOption::DistanceUnits));
        units.original_finances = options.flag(config::game::GameOption::OriginalFinances);
        return units;
    }

    std::optional<usize> pool_room(const ParkScene& scene, const save::model::PlacedObject& o)
    {
        if (!o.fixture || !std::ranges::contains(scene.pool_pieces, o.id))
            return std::nullopt;
        const auto room = std::ranges::find_if(scene.pools.rooms, [&](const rct3::park::pools::PoolRoom& r) { return r.col == o.col && r.row == o.row && r.symbol == o.symbol; });
        return room == scene.pools.rooms.end() ? std::nullopt : std::optional(static_cast<usize>(room - scene.pools.rooms.begin()));
    }

    entt::entity room_entity(const ParkScene& scene, usize room)
    {
        for (const auto [e, door] : scene.sim.registry.view<const simulation::PoolDoor>().each())
            if (door.col == scene.pools.rooms[room].col && door.row == scene.pools.rooms[room].row)
                return e;
        return entt::null;
    }

    entt::entity animal_house_of(const ParkScene& scene, const save::model::PlacedObject& o)
    {
        // The park keeps two records of each house and hut, alike but for their ids, so either is known by its place.
        const auto same = [&](u64 id) {
            const auto placed = std::ranges::find(scene.objects, id, &save::model::PlacedObject::id);
            return placed != scene.objects.end() && placed->symbol == o.symbol && placed->col == o.col && placed->row == o.row;
        };
        for (const auto [e, house] : scene.sim.registry.view<const simulation::AnimalHouse>().each())
            if (same(house.object) || (house.hut != 0 && same(house.hut)))
                return e;
        return entt::null;
    }

    ui::screens::info::SceneryFacts prop_facts(const ParkScene& scene, save::model::PlacedObjectIndex index, const ui::StringTable& strings)
    {
        ui::screens::info::SceneryFacts f;
        if (index.value >= scene.objects.size())
            return f;
        const save::model::PlacedObject& o = scene.objects[index.value];
        f.symbol = o.symbol;
        f.colours = o.colours;
        f.zero_time = scene.park.zero_time;
        if (const auto model = scene.models.find(rct3::park::object::model_ref(o)); model != scene.models.end())
            f.colour_slots = rct3::park::object::colour_slots(model->second);
        if (const auto item = listed_scenery(scene, o))
            f.list_icon = item->icon;
        if (const auto bin = std::ranges::find(scene.bins, o.id, &save::model::BinState::object_ref); bin != scene.bins.end())
            f.bin = &*bin;
        if (const auto sign = std::ranges::find(scene.signs, o.id, &save::model::SignState::object_ref); sign != scene.signs.end())
            f.sign = &*sign;
        const auto items = scene.sim.registry.view<const simulation::PathItem>();
        const auto* rules = scene.sim.registry.ctx().find<simulation::PassportRules>();
        if (rules != nullptr && std::ranges::any_of(items, [&](entt::entity e) {
                const auto& i = items.get<const simulation::PathItem>(e);
                return i.object == o.id && i.kind == simulation::PathItemKind::PassportPost;
            }))
        {
            f.passport = ui::screens::info::PassportFacts { .stamps_for_gift = static_cast<i32>(rules->stamps_for_gift), .most_stamps = static_cast<i32>(simulation::kTuning.surroundings.most_stamps) };
            for (const rct3::park::CarriedKind* gift : simulation::passport_gifts(scene.carried))
            {
                if (gift->symbol == rules->gift)
                    f.passport->gift = static_cast<i32>(f.passport->gifts.size());
                f.passport->gifts.push_back(gift->name_text);
            }
        }
        if (const entt::entity home = animal_house_of(scene, o); home != entt::null)
        {
            // The species that need this kind of house, each once though the catalogue lists four animals of each.
            f.house.emplace();
            const auto& r = scene.sim.registry;
            f.house->schedule = r.get<simulation::AnimalHouse>(home).schedule;
            const bool listed = scene.build.items.valid() && scene.build.items.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
            for (const rct3::park::BuildItem& item : listed ? std::span<const rct3::park::BuildItem>(scene.build.items.get()) : std::span<const rct3::park::BuildItem> { })
                if (item.kind == rct3::park::BuildKind::Animal && strings::equals_ignoring_case(item.needs, o.symbol) && !std::ranges::contains(f.house->species, std::string_view(item.group_text)))
                    f.house->species.push_back(item.group_text);
            for (const auto [k, duty] : r.view<const simulation::KeeperDuty>().each())
                if (duty.house == home)
                    if (const auto shown = std::ranges::find(scene.shown_people, k, &ShownPerson::guest); shown != scene.shown_people.end() && shown->staff && shown->saved < scene.people.staff.size())
                        f.house->keepers.push_back(ui::screens::info::person_name({ .staff = &scene.people.staff[shown->saved] }, strings));
        }
        if (const auto room = pool_room(scene, o))
        {
            // Its complex's swimmers, and its water's dirt, as the simulation has them.
            const auto& r = scene.sim.registry;
            const entt::entity e = room_entity(scene, *room);
            const auto* door = e != entt::null ? &r.get<simulation::PoolDoor>(e) : nullptr;
            const auto* pool = door != nullptr && r.valid(door->pool) ? r.try_get<simulation::SwimPool>(door->pool) : nullptr;
            i64 people = 0;
            for (const auto [g, swimmer, errand] : r.view<const simulation::Swimmer, const simulation::Errand>().each())
                people += door != nullptr && r.valid(errand.target) && r.all_of<simulation::PoolDoor>(errand.target) && r.get<simulation::PoolDoor>(errand.target).pool == door->pool;
            const rct3::park::pools::PoolRoom& at = scene.pools.rooms[*room];
            f.pool = ui::screens::info::PoolComplexFacts {
                .name = strings::to_utf16(at.complex_name), .number = static_cast<usize>(std::ranges::count(scene.pools.rooms.begin(), scene.pools.rooms.begin() + static_cast<std::ptrdiff_t>(*room) + 1, at.symbol, &rct3::park::pools::PoolRoom::symbol)), .ticket = units::Money { at.ticket }, .open = at.open, .people = people, .dirt = pool != nullptr ? simulation::dirt_step(*pool) : 0, .fluid = at.fluid, .waves = at.waves, .refill = simulation::kTuning.pools.refill.at(std::min<usize>(at.fluid, 2))
            };
            // The room's takings and upkeep, and what guests thought of it, as the simulation has them.
            if (const auto* attraction = e != entt::null ? r.try_get<simulation::Attraction>(e) : nullptr)
            {
                f.pool->customers = attraction->customers;
                f.pool->income = attraction->income;
            }
            if (const auto* upkeep = e != entt::null ? r.try_get<simulation::Upkeep>(e) : nullptr)
            {
                f.pool->reliability = upkeep->reliability.value;
                f.pool->since_inspection = upkeep->since_inspection;
                if (const auto interval = std::ranges::find(simulation::kInspectionSeconds, upkeep->inspect_every); interval != simulation::kInspectionSeconds.end())
                    f.pool->inspection = static_cast<i32>(interval - simulation::kInspectionSeconds.begin());
            }
            for (const auto [g, thoughts] : r.view<const simulation::Thoughts>().each())
                for (const simulation::Thought& t : thoughts.recent)
                    if (e != entt::null && t.about == e)
                        f.pool->thoughts.push_back({ .thought = static_cast<i32>(std::to_underlying(t.kind)), .time = t.time.value, .variant = t.variant });
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
        const usize skins = ui::add_extra_skins(device, assets, names);
        logging::logger("park").info("{} carried items, {} extras, {} pictures", scene.carried.items.size(), scene.carried.extras.size(), skins);
    }

    void mark_train(const ParkScene& scene, ui::screens::info::InfoPanel& panel)
    {
        if (!scene.shown || scene.shown->kind != rct3::park::AttractionRef::Kind::Ride || scene.info_tab != ui::screens::info::AttractionTab::Graphs || !scene.simulate)
            return;
        const usize index = scene.shown->index;
        if (index >= scene.rides.rides.size() || index >= scene.ride_entities.size() || scene.ride_entities[index] == entt::null)
            return;
        panel.mark_graph(ui::screens::info::graph_marker(scene.rides.rides[index], lap_to_go(scene.sim.registry.try_get<simulation::Trains>(scene.ride_entities[index]))));
    }

    void show_info_page(ParkScene& scene, ui::screens::info::InfoPanel& panel, const ui::InterfaceAssets& assets, const config::game::GameOptions& options)
    {
        if (!scene.shown_party.empty())
            panel.show_page(ui::screens::info::party_page(scene.party_tab, party_facts(scene), *assets.strings, info_units(assets, options)));
        else if (scene.shown_prop)
        {
            ui::screens::info::SceneryFacts f = prop_facts(scene, *scene.shown_prop, *assets.strings);
            if (f.pool)
                f.pool->currency = info_units(assets, options).currency;
            if (f.house)
                f.house->currency = info_units(assets, options).currency;
            panel.show_page(ui::screens::info::scenery_page(scene.prop_tab, f, *assets.strings));
        }
        else if (scene.shown_animal)
        {
            const auto& r = scene.sim.registry;
            ui::screens::info::AnimalFacts f;
            const auto* animal = r.try_get<simulation::Animal>(*scene.shown_animal);
            if (animal != nullptr)
            {
                f.animal = *animal;
                // The kind's facts, from its own animal in the catalogue: each kind lists its adult male, adult
                // female, young male and young female in turn.
                const bool listed = scene.build.items.valid() && scene.build.items.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
                const std::span<const rct3::park::BuildItem> catalogue = listed ? std::span<const rct3::park::BuildItem>(scene.build.items.get()) : std::span<const rct3::park::BuildItem> { };
                usize nth = (animal->adult ? 0 : 2) + (animal->male ? 0 : 1);
                for (const rct3::park::BuildItem& item : catalogue)
                {
                    if (item.kind != rct3::park::BuildKind::Animal || !strings::equals_ignoring_case(item.symbol, animal->species))
                        continue;
                    const bool its_own = nth-- == 0;
                    if (f.species_text.empty() || its_own)
                    {
                        f.species_text = item.group_text;
                        f.species_own_name = item.species_name;
                        f.rarity = item.rarity;
                        f.space = item.care.space;
                        const auto house = std::ranges::find_if(catalogue, [&](const rct3::park::BuildItem& s) { return s.kind == rct3::park::BuildKind::Shelter && strings::equals_ignoring_case(s.symbol, item.needs); });
                        f.house_text = house != catalogue.end() ? std::string_view(house->name_text) : std::string_view { };
                    }
                    if (its_own)
                        break;
                }
                if (const auto* care = r.try_get<simulation::AnimalCare>(*scene.shown_animal))
                    f.habitat = care->habitat;
                // Its enclosure is clean while the dung in it stays under what its animals put up with.
                if (animal->enclosure)
                {
                    i32 animals = 0;
                    i32 dung = 0;
                    for (const auto [e, other] : r.view<const simulation::Animal>().each())
                        animals += other.enclosure == animal->enclosure;
                    for (const auto [e, piece] : r.view<const simulation::Dung>().each())
                        dung += piece.enclosure == animal->enclosure;
                    f.cleanliness = units::Share { 1 - std::clamp(static_cast<f32>(dung) / static_cast<f32>(std::max(animals, 1) * simulation::kAnimalRates.dung_limit), 0.0f, 1.0f) };
                }
            }
            panel.show_page(ui::screens::info::animal_page(scene.animal_tab, f, *assets.strings));
        }
        else if (scene.shown_person)
            panel.show_page(ui::screens::info::person_page(scene.person_tab, person_facts(scene, *scene.shown_person), *assets.strings, info_units(assets, options)));
        else if (scene.shown)
        {
            // A tracked ride's train limits are read the first time its window shows them.
            if (scene.shown->kind == rct3::park::AttractionRef::Kind::Ride && assets.install)
                track::ready_trains(scene, *assets.install, scene.shown->ride());
            panel.show_page(ui::screens::info::attraction_page(scene.info_tab, facts(scene, *scene.shown), *assets.strings, info_units(assets, options), scene.info_section));
        }
    }

    rct3::park::ParkCamera person_view(const ParkScene& scene, usize shown, f32 yaw)
    {
        rct3::park::ParkCamera camera;
        camera.target = glm::vec3(scene.shown_people[shown].pose.world[3]) + glm::vec3(0, kPersonViewHeight, 0);
        camera.yaw = yaw;
        camera.pitch = kViewPitch.value;
        camera.distance = kPersonViewDistance;
        return camera;
    }

    rct3::park::ParkCamera animal_view(const ParkScene& scene, entt::entity animal, f32 yaw)
    {
        const DrawnAnimals& drawn = scene.drawn_animals;
        std::vector<glm::vec3> corners;
        if (const auto at = std::ranges::find(drawn.placed_animals, animal); at != drawn.placed_animals.end())
        {
            const rct3::park::PlacedAnimal& placed = drawn.placed[static_cast<usize>(at - drawn.placed_animals.begin())];
            for (const glm::vec3 c : { placed.body->low, placed.body->high })
                corners.emplace_back(placed.world * glm::vec4(c, 1.0f));
        }
        else if (const auto* pose = scene.sim.registry.try_get<simulation::Pose>(animal))
        {
            // In its crate: the crate's size, about.
            constexpr glm::vec3 kCrate { 0.8f, 1.6f, 0.8f };
            corners = { pose->position - glm::vec3(kCrate.x, 0, kCrate.z), pose->position + kCrate };
        }
        return rct3::park::framed(corners, yaw, kViewPitch.value, kPropViewClosest);
    }

    void open_animal(ParkScene& scene, ui::screens::info::InfoPanel& panel, entt::entity animal, const ui::InterfaceAssets& assets, const config::game::GameOptions& options)
    {
        if (!scene.sim.registry.all_of<simulation::Animal>(animal))
            return;
        std::vector<ui::screens::info::InfoTab> tabs;
        usize picked = 0;
        for (const ui::screens::info::AnimalTabInfo& t : ui::screens::info::kAnimalTabs)
        {
            if (t.tab == scene.animal_tab)
                picked = tabs.size();
            tabs.push_back({ .icon = std::string(t.icon), .help = std::u16string(assets.strings->get(t.help)), .slot = t.slot });
        }
        if (scene.shown_animal != animal)
            scene.view_camera = animal_view(scene, animal, scene.camera.yaw);
        scene.shown.reset();
        scene.shown_person.reset();
        scene.shown_prop.reset();
        scene.shown_party.clear();
        scene.shown_animal = animal;
        panel.show(std::move(tabs), picked);
        show_info_page(scene, panel, assets, options);
    }

    void open_person(ParkScene& scene, ui::screens::info::InfoPanel& panel, usize shown, const ui::InterfaceAssets& assets, const config::game::GameOptions& options)
    {
        if (shown >= scene.shown_people.size())
            return;
        std::vector<ui::screens::info::InfoTab> tabs;
        scene.person_tabs.clear();
        usize picked = 0;
        const ui::screens::info::PersonFacts f = person_facts(scene, shown);
        for (const ui::screens::info::PersonTabInfo& t : ui::screens::info::person_tabs(f))
        {
            if (t.tab == scene.person_tab)
                picked = tabs.size();
            ui::screens::info::InfoTab& tab = tabs.emplace_back();
            tab.icon = t.icon;
            tab.help = assets.strings->get(t.help);
            tab.slot = t.slot;
            // A uniform's pots show the colours the person is drawn in.
            for (usize pot = 0; pot < t.pots; ++pot)
                tab.paints.push_back(scene.shown_people[shown].pose.look.colours.at(ui::screens::info::kUpperPalette + pot));
            tab.colours = ui::screens::info::kUniformColours;
            tab.action = t.tab == ui::screens::info::PersonTab::Party;
            scene.person_tabs.push_back(t.tab);
        }
        // A uniform tab has no page of its own, nor the party's, which opens the party.
        if (scene.person_tabs.at(picked) == ui::screens::info::PersonTab::Uniform || scene.person_tabs.at(picked) == ui::screens::info::PersonTab::Party)
            picked = 0;
        scene.person_tab = scene.person_tabs.at(picked);
        if (scene.shown_person != shown)
        {
            scene.view_camera = person_view(scene, shown, scene.camera.yaw);
            scene.painting_patrol = false;
        }
        scene.shown_animal.reset();
        scene.shown.reset();
        scene.shown_prop.reset();
        scene.shown_party.clear();
        scene.shown_person = shown;
        panel.show(std::move(tabs), picked);
        show_info_page(scene, panel, assets, options);
    }

    void open_party(ParkScene& scene, ui::screens::info::InfoPanel& panel, usize shown, const ui::InterfaceAssets& assets, const config::game::GameOptions& options)
    {
        std::vector<ui::screens::info::InfoTab> tabs;
        usize picked = 0;
        for (const ui::screens::info::PartyTabInfo& t : ui::screens::info::kPartyTabs)
        {
            if (t.tab == scene.party_tab)
                picked = tabs.size();
            tabs.push_back({ .icon = std::string(t.icon), .help = std::u16string(assets.strings->get(t.help)), .slot = t.slot });
        }
        scene.shown_animal.reset();
        scene.shown_party = party_of(scene, shown);
        scene.shown.reset();
        scene.shown_person.reset();
        scene.shown_prop.reset();
        panel.show(std::move(tabs), picked);
        show_info_page(scene, panel, assets, options);
    }

    void open_info(ParkScene& scene, ui::screens::info::InfoPanel& panel, rct3::park::AttractionRef ref, const ui::InterfaceAssets& assets, const config::game::GameOptions& options, std::optional<ui::screens::info::AttractionTab> tab)
    {
        const ui::screens::info::AttractionFacts f = facts(scene, ref);
        if (f.common == nullptr)
            return;
        const ui::screens::info::AttractionKind kind = f.stall ? ui::screens::info::AttractionKind::Stall : ui::screens::info::attraction_kind(*f.ride);
        if (const bool tracked = f.ride != nullptr && f.ride->kind == save::model::RideKind::Tracked; tracked != scene.info_tracked)
        {
            std::swap(scene.info_tab, scene.other_info_tab);
            scene.info_tracked = tracked;
        }
        if (tab)
            scene.info_tab = *tab;
        std::vector<ui::screens::info::InfoTab> tabs;
        scene.shown_tabs.clear();
        usize picked = 0;
        for (const ui::screens::info::AttractionTabInfo& t : ui::screens::info::attraction_tabs(kind, f.common->state))
        {
            if (t.tab == scene.info_tab)
                picked = tabs.size();
            ui::screens::info::InfoTab& tab = tabs.emplace_back();
            tab.icon = t.icon;
            tab.help = assets.strings->get(t.help);
            tab.slot = t.slot;
            tab.action = t.action;
            if (t.tab == ui::screens::info::AttractionTab::State)
                tab.choices = state_fan(scene, ref, *assets.strings);
            scene.shown_tabs.push_back(t.tab);
        }
        // The open-or-close tab has no page of its own.
        if (scene.shown_tabs.at(picked) == ui::screens::info::AttractionTab::State)
            picked = 0;
        scene.info_tab = scene.shown_tabs.at(picked);
        if (scene.shown != ref || scene.shown_person || !scene.shown_party.empty())
        {
            // The view starts from where the player looks from, and turns from there.
            std::vector<glm::vec3> points;
            for (usize i = 0; i < scene.objects.size() && i < scene.anchors.size(); ++i)
            {
                const auto owner = scene.attractions.find(scene.objects[i].id);
                if (owner != scene.attractions.end() && owner->second == ref)
                    points.push_back(scene.anchors[i]);
            }
            scene.view_camera = rct3::park::framed(points, scene.camera.yaw, kViewPitch.value);
        }
        scene.shown_animal.reset();
        scene.shown_person.reset();
        scene.shown_prop.reset();
        scene.shown_party.clear();
        if (scene.shown != ref)
            scene.refusal.clear();
        scene.shown = ref;
        panel.show(std::move(tabs), picked);
        show_info_page(scene, panel, assets, options);
    }

    void open_prop(ParkScene& scene, ui::screens::info::InfoPanel& panel, save::model::PlacedObjectIndex index, const ui::InterfaceAssets& assets, const config::game::GameOptions& options, std::optional<ui::screens::info::SceneryTab> open)
    {
        if (index.value >= scene.objects.size())
            return;
        if (const entt::entity home = animal_house_of(scene, scene.objects[index.value]); home != entt::null)
        {
            const u64 house = scene.sim.registry.get<simulation::AnimalHouse>(home).object;
            if (const auto found = std::ranges::find(scene.objects, house, &save::model::PlacedObject::id); found != scene.objects.end())
                index = save::model::PlacedObjectIndex { static_cast<usize>(found - scene.objects.begin()) };
            open = open.value_or(ui::screens::info::SceneryTab::HouseDetails);
        }
        std::vector<ui::screens::info::InfoTab> tabs;
        scene.prop_tabs.clear();
        for (const ui::screens::info::SceneryTabInfo& t : ui::screens::info::scenery_tabs(prop_facts(scene, index, *assets.strings)))
        {
            ui::screens::info::InfoTab& tab = tabs.emplace_back();
            tab.icon = t.icon;
            tab.help = assets.strings->get(t.help);
            tab.slot = t.slot;
            tab.action = t.action;
            tab.paints = t.paints;
            // A changing room's flag opens or closes its complex, as a stall's does.
            if (t.tab == ui::screens::info::SceneryTab::RoomState)
                for (const ui::screens::info::StateChoice& c : ui::screens::info::state_choices(ui::screens::info::AttractionKind::Stall))
                    tab.choices.push_back({ std::string(c.icon), std::u16string(assets.strings->get(c.help)) });
            scene.prop_tabs.push_back(t.tab);
        }
        if (scene.shown_prop != index)
            scene.view_camera = rct3::park::framed(rct3::park::object::object_corners(scene.objects[index.value], scene.models, scene.grid, scene.fitted), scene.camera.yaw, kViewPitch.value, kPropViewClosest);
        scene.shown_animal.reset();
        scene.shown.reset();
        scene.shown_person.reset();
        scene.shown_party.clear();
        scene.shown_prop = index;
        const auto picked = open ? std::ranges::find(scene.prop_tabs, *open) : scene.prop_tabs.end();
        scene.prop_tab = picked != scene.prop_tabs.end() ? open : std::nullopt;
        const auto at = static_cast<usize>(picked - scene.prop_tabs.begin());
        panel.show(std::move(tabs), at);
        show_info_page(scene, panel, assets, options);
    }

    void change_attraction(render::Device& device, ParkScene& scene, ui::screens::info::InfoPanel& panel, rct3::park::AttractionRef ref, const ui::screens::info::InfoChange& change, const ui::InterfaceAssets& assets, const config::game::GameOptions& options)
    {
        using enum ui::screens::info::AttractionControl;
        save::model::Ride* ride = ref.kind == rct3::park::AttractionRef::Kind::Ride && ref.index < scene.rides.rides.size() ? &scene.rides.rides[ref.index] : nullptr;
        save::model::Stall* stall = ref.kind == rct3::park::AttractionRef::Kind::Stall && ref.index < scene.rides.stalls.size() ? &scene.rides.stalls[ref.index] : nullptr;
        const auto set_ride = [&](simulation::actions::RideSetting setting, std::optional<i32> value) { simulation::actions::ask(scene.actions, scene.sim, simulation::actions::ChangeRide { .ride = ride->base.id, .setting = setting, .value = value }); };
        const auto wait_on = [&](simulation::actions::RideSetting wait, ui::screens::info::AttractionControl seconds) {
            const auto field = panel.control(ui::screens::info::control_id(seconds));
            set_ride(wait, change.value != 0 ? std::optional(field ? static_cast<const ui::widgets::Spinner&>(*field).value() : 0) : std::nullopt);
            if (field)
                field->enabled = change.value != 0;
        };
        if (change.id == ui::screens::info::control_id(Colours))
        {
            // Every object of the attraction takes the colour in the slot of the pot.
            const auto painted = painted_object(scene, ref);
            if (!painted || change.pot >= painted->second.size())
                return;
            if (const save::model::AttractionCommon* common = ride != nullptr ? &ride->base : stall != nullptr ? &stall->base : nullptr)
                simulation::actions::ask(scene.actions, scene.sim, simulation::actions::PaintAttraction { .attraction = common->id, .slot = painted->second[change.pot], .colour = change.value });
        }
        else if (change.id == ui::screens::info::control_id(Name))
        {
            if (const save::model::AttractionCommon* common = ride != nullptr ? &ride->base : stall != nullptr ? &stall->base : nullptr)
                simulation::actions::ask(scene.actions, scene.sim, simulation::actions::RenameAttraction { .attraction = common->id, .name = strings::to_utf8(change.text) });
        }
        else if (change.id == ui::screens::info::control_id(Locate))
        {
            // The view turns round to the middle of the attraction's objects.
            glm::vec3 sum { };
            usize count = 0;
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
                scene.camera.target = sum / static_cast<f32>(count);
        }
        else if (change.id == ui::screens::info::control_id(RideCam) && ride != nullptr)
            ride_cam(scene, assets, ref.ride());
        else if (stall != nullptr)
        {
            using simulation::actions::StallEdit;
            simulation::actions::ChangeStall edit { .stall = stall->base.id, .value = change.value, .pot = change.pot };
            if (change.id == ui::screens::info::control_id(SyncStalls))
                edit.edit = StallEdit::Sync;
            else if (change.id == ui::screens::info::control_id(SellsAs))
            {
                const std::vector<const rct3::park::BuildItem*> kinds = stall_kinds(scene);
                if (change.value < 1 || std::cmp_greater(change.value, kinds.size()))
                    return;
                const rct3::park::BuildItem& kind = *kinds[static_cast<usize>(change.value - 1)];
                edit.edit = StallEdit::SellAs;
                edit.text = std::format("{}:sta", kind.symbol);
                edit.overlay = kind.overlay;
                for (const rct3::park::BuildItem::Goods& g : kind.goods)
                    edit.items.push_back({ .kind_symbol = std::format("{}:cid", g.item), .price = { g.price }, .name = "%ME%" });
            }
            else if (const auto item = ui::screens::info::listed(change.id, ItemPrice))
                edit = { .stall = edit.stall, .edit = StallEdit::ItemPrice, .item = *item, .value = change.value };
            else if (const auto gone = ui::screens::info::listed(change.id, ItemRemove))
                edit = { .stall = edit.stall, .edit = StallEdit::RemoveItem, .item = *gone };
            else if (const auto added = ui::screens::info::listed(change.id, ItemAdd))
            {
                const rct3::park::BuildItem* kind = selling_kind(scene, *stall);
                if (kind == nullptr || *added >= kind->goods.size())
                    return;
                const rct3::park::BuildItem::Goods& g = kind->goods[*added];
                // shortcut: it goes on at its kind's own price, as a new stall's items do; upgrade alongside them.
                edit.edit = StallEdit::AddItem;
                edit.items = { { .kind_symbol = std::format("{}:cid", g.item), .price = { g.price }, .name = "%ME%" } };
            }
            else if (const auto moved = ui::screens::info::listed(change.id, ItemMove))
                edit = { .stall = edit.stall, .edit = StallEdit::MoveItem, .item = *moved, .value = change.value };
            else if (const auto ticked = ui::screens::info::listed(change.id, ItemExtras))
                edit = { .stall = edit.stall, .edit = StallEdit::ItemExtras, .item = *ticked, .value = change.value };
            else if (const auto named = ui::screens::info::listed(change.id, ItemName))
                edit = { .stall = edit.stall, .edit = StallEdit::ItemName, .item = *named, .text = strings::to_utf8(change.text) };
            else if (const auto painted = ui::screens::info::listed(change.id, ItemColour))
                edit = { .stall = edit.stall, .edit = StallEdit::ItemColour, .item = *painted, .value = change.value, .pot = change.pot };
            else if (const auto extra = ui::screens::info::listed(change.id, ExtraLevel))
                edit = { .stall = edit.stall, .edit = StallEdit::ExtraLevel, .item = *extra, .value = change.value };
            else
                return;
            simulation::actions::ask(scene.actions, scene.sim, std::move(edit));
            // The menu's rows follow what changed; a price typed in stays as it is.
            if (!ui::screens::info::listed(change.id, ItemPrice))
                scene.info_after_actions = true;
        }
        else if (ride == nullptr)
            return;
        else if (change.id == ui::screens::info::control_id(Price))
            set_ride(simulation::actions::RideSetting::TicketPrice, change.value);
        else if (change.id == ui::screens::info::control_id(Inspection))
            set_ride(simulation::actions::RideSetting::InspectEvery, change.value);
        else if (change.id == ui::screens::info::control_id(Mechanic))
            simulation::actions::ask(scene.actions, scene.sim, simulation::actions::CallMechanic { .ride = ride->base.id });
        else if (change.id == ui::screens::info::control_id(MinWaitOn))
            wait_on(simulation::actions::RideSetting::MinWait, MinWait);
        else if (change.id == ui::screens::info::control_id(MaxWaitOn))
            wait_on(simulation::actions::RideSetting::MaxWait, MaxWait);
        else if (change.id == ui::screens::info::control_id(MinWait))
            set_ride(simulation::actions::RideSetting::MinWait, change.value);
        else if (change.id == ui::screens::info::control_id(MaxWait))
            set_ride(simulation::actions::RideSetting::MaxWait, change.value);
        else if (change.id == ui::screens::info::control_id(WaitFor))
            set_ride(simulation::actions::RideSetting::WaitForLoad, change.value);
        else if ((change.id == ui::screens::info::control_id(Trains) || change.id == ui::screens::info::control_id(Cars)) && scene.train_limits.contains(ref.ride()) && ride->base.state != save::model::AttractionState::Open && assets.install)
        {
            const bool trains = change.id == ui::screens::info::control_id(Trains);
            (void)track::set_trains(device, scene, *assets.install, ref.ride(), trains ? change.value : ride->train_count.value_or(0), trains ? ride->cars_per_train.value_or(0) : change.value);
        }
        else if (change.id == ui::screens::info::control_id(CarKind) && scene.train_limits.contains(ref.ride()) && ride->base.state != save::model::AttractionState::Open && assets.install && !ride->trains.empty())
        {
            // The next kind of train its kind runs, round to the first after the last.
            const std::vector<std::string>& choices = scene.train_choices[ref.ride()];
            const std::string_view symbol = ride->trains.front().symbol;
            const std::string_view now = symbol.substr(0, symbol.find(':'));
            const auto at = std::ranges::find_if(choices, [&](const std::string& c) { return strings::equals_ignoring_case(c, now); });
            if (choices.size() > 1)
                if (track::use_train(scene, *assets.install, ref.ride(), choices[at == choices.end() ? 0 : static_cast<usize>(at - choices.begin() + 1) % choices.size()]))
                {
                    (void)track::set_trains(device, scene, *assets.install, ref.ride(), ride->train_count.value_or(0), ride->cars_per_train.value_or(0));
                    track::update_platforms(scene, ref.ride(), assets);
                }
        }
        else if (change.id == ui::screens::info::control_id(Circuits))
            set_ride(simulation::actions::RideSetting::Circuits, change.value);
        else if (const auto show = std::ranges::find(scene.shows, ride->base.id, &save::model::RideShow::ride); show != scene.shows.end() && (change.id == ui::screens::info::control_id(ShowTrick) || change.id == ui::screens::info::control_id(ShowDelete)))
        {
            const usize place = std::min(scene.info_section, show->order.size());
            const std::vector<u32> offers = ui::screens::info::show_offers(*show);
            if (change.id == ui::screens::info::control_id(ShowDelete))
                simulation::actions::ask(scene.actions, scene.sim, simulation::actions::EditShow { .ride = ride->base.id, .place = place });
            else if (change.value >= 0 && std::cmp_less(change.value, offers.size()))
                simulation::actions::ask(scene.actions, scene.sim, simulation::actions::EditShow { .ride = ride->base.id, .place = place, .trick = offers[static_cast<usize>(change.value)] });
        }
        else if (change.id == ui::screens::info::control_id(BlockSection))
            set_ride(simulation::actions::RideSetting::BlockSections, change.value);
        else if (change.id == ui::screens::info::control_id(SyncStations))
            set_ride(simulation::actions::RideSetting::SyncStations, change.value);
        else if (change.id == ui::screens::info::control_id(LeaveOnArrival))
            set_ride(simulation::actions::RideSetting::LeaveOnArrival, change.value);
        else if (change.id == ui::screens::info::control_id(LowFriction))
        {
            if (change.value != 0)
                scene.low_friction.insert(ride->base.id);
            else
                scene.low_friction.erase(ride->base.id);
        }
        else if (change.id == ui::screens::info::control_id(LaunchSpeed) || change.id == ui::screens::info::control_id(LiftSpeed))
        {
            // The page shows hundredths of the player's unit.
            const f32 speed = static_cast<f32>(change.value) / 100 / ui::screens::info::speed_scale(info_units(assets, options).distance);
            simulation::actions::ask(scene.actions, scene.sim, simulation::actions::SetRideSpeed { .ride = ride->base.id, .launch = change.id == ui::screens::info::control_id(LaunchSpeed), .speed = speed });
        }
        else if (change.id >= ui::screens::info::control_id(SpeedSeries) && change.id <= ui::screens::info::control_id(LateralSeries))
        {
            const auto bit = static_cast<u8>(1u << (change.id - ui::screens::info::control_id(SpeedSeries)));
            u8& shown = scene.graph_series.try_emplace(ride->base.id, ui::screens::info::kSpeedOnly).first->second;
            shown = static_cast<u8>(change.value != 0 ? shown | bit : shown & ~bit);
        }
        else if (change.id == ui::screens::info::control_id(Music))
            simulation::actions::ask(scene.actions, scene.sim, simulation::actions::SetRideMusic { .ride = ride->base.id, .file = audio::tune_file(static_cast<usize>(std::max(change.value, 0)), scene.tunes) });
        else if (change.id == ui::screens::info::control_id(Fence))
            scene.ride_styles_shown = !scene.ride_styles_shown;
        else if (const bool fence = change.id == ui::screens::info::control_id(FenceStyle); fence || change.id == ui::screens::info::control_id(EntranceStyle) || change.id == ui::screens::info::control_id(ExitStyle))
        {
            if (!scene.build.items.valid() || scene.build.items.wait_for(std::chrono::seconds(0)) != std::future_status::ready || change.value < 0)
                return;
            const rct3::park::ride::RideStyles styles = rct3::park::ride::ride_styles(scene.build.items.get());
            const std::vector<rct3::park::ride::ExtraStyle>& list = fence ? styles.fences : styles.doors;
            if (std::cmp_greater_equal(change.value, list.size()))
                return;
            const rct3::park::ride::ExtraStyle& style = list[static_cast<usize>(change.value)];
            wait_for_build(scene); // The build beside the frame reads the models loaded here
            if (fence)
            {
                // In the fence's own colours, which its model has; one that does not load keeps the ride's.
                simulation::actions::SetRideFence restyle { .ride = ride->base.id, .symbol = style.symbol, .overlay = style.overlay, .colours = ride->fence_colours };
                if (const save::model::PlacedObject panel { .symbol = style.symbol, .overlay = style.overlay }; !style.symbol.empty() && load_object_model(scene, scene.placing.models, assets, panel))
                    std::ranges::transform(scene.models.at(rct3::park::object::model_key(panel)).default_colours, restyle.colours.begin(), [](i32 c) { return static_cast<u8>(c); });
                simulation::actions::ask(scene.actions, scene.sim, std::move(restyle));
            }
            else
            {
                const bool exits = change.id == ui::screens::info::control_id(ExitStyle);
                const std::string& symbol = exits ? style.exit_symbol : style.symbol;
                if (load_object_model(scene, scene.placing.models, assets, { .symbol = symbol, .overlay = style.overlay }))
                    simulation::actions::ask(scene.actions, scene.sim, simulation::actions::SetRideDoors { .ride = ride->base.id, .exits = exits, .symbol = symbol, .overlay = style.overlay });
            }
        }
        else if (change.id == ui::screens::info::control_id(QueueSigns))
        {
            // The button turns them round.
            if (scene.led_signs.hidden.contains(ride->base.id))
                scene.led_signs.hidden.erase(ride->base.id);
            else
                scene.led_signs.hidden.insert(ride->base.id);
            scene.led_signs.stale = true;
        }
    }

    void open_row(ParkScene& scene, ui::screens::info::InfoPanel& panel, ui::screens::management::ManagementRow row, const ui::InterfaceAssets& assets, const config::game::GameOptions& options)
    {
        using Kind = ui::screens::management::ManagementRow::Kind;
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
            open_person(scene, panel, static_cast<usize>(shown - scene.shown_people.begin()), assets, options);
    }

} // namespace rct3::scene::park
