// SPDX-License-Identifier: GPL-3.0-or-later
//
// sim-run: run the simulation headless on an original save and report what happened.
//
//   sim-run SAVE [--seconds N] [--threads N] [--seed N] [--game DIR] [--weather rain|storm|hot|cold] [--whole-day]
//
// Brings over the save's paths, stalls, rides and guests, runs N seconds of park time (default 600) and prints
// what each shop and ride took and how the guests are doing, with how long the run took. Flat rides take
// their timing from their models, so they come over only with --game, the game's folder.
#include "config/game_install.hpp"
#include "park/asset_index.hpp"
#include "park/build_catalogue.hpp"
#include "park/item_category.hpp"
#include "park/object_mesh.hpp"
#include "park/object_models.hpp"
#include "park/track_course.hpp"
#include "park/track_pieces.hpp"
#include "save/model/park.hpp"
#include "save/model/people.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"
#include "save/save_file.hpp"
#include "simulation/coaster.hpp"
#include "simulation/components.hpp"
#include "simulation/import_park.hpp"
#include "simulation/import_people.hpp"
#include "simulation/park_state.hpp"
#include "simulation/ratings.hpp"
#include "simulation/systems.hpp"
#include "simulation/workers.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

int main(int argc, char** argv)
{
    using namespace rct3;
    const char* path = nullptr;
    float seconds = 600;
    unsigned threads = 0;
    std::uint64_t seed = 1;
    const char* game = nullptr;
    std::string_view weather;
    bool whole_day = false; // Run the clock through the night, whatever the park's day mode
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc)
            seconds = std::strtof(argv[++i], nullptr);
        else if (std::strcmp(argv[i], "--threads") == 0 && i + 1 < argc)
            threads = static_cast<unsigned>(std::strtoul(argv[++i], nullptr, 10));
        else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
            seed = std::strtoull(argv[++i], nullptr, 10);
        else if (std::strcmp(argv[i], "--game") == 0 && i + 1 < argc)
            game = argv[++i];
        else if (std::strcmp(argv[i], "--whole-day") == 0)
            whole_day = true;
        else if (std::strcmp(argv[i], "--weather") == 0 && i + 1 < argc)
            weather = argv[++i];
        else
            path = argv[i];
    }
    if (!path)
    {
        std::fprintf(stderr, "usage: sim-run SAVE [--seconds N] [--threads N] [--seed N] [--game DIR] [--weather rain|storm|hot|cold] [--whole-day]\n");
        return 2;
    }
    std::ifstream in(path, std::ios::binary);
    const std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!in && raw.empty())
    {
        std::fprintf(stderr, "%s: cannot read\n", path);
        return 1;
    }
    const auto file = save::read_save(std::as_bytes(std::span(raw)), { .verify_checksum = false });
    if (!file)
    {
        std::fprintf(stderr, "%s: %s\n", path, save::to_string(file.error()));
        return 1;
    }
    const save::ObjectIndex index(*file);
    const auto people = save::load_people(*file, index);
    const auto world_model = save::load_world(*file, index);
    const auto rides = save::load_rides(*file, index);
    if (!people || !world_model || !rides)
    {
        std::fprintf(stderr, "%s: cannot load people, world or rides\n", path);
        return 1;
    }

    simulation::SimWorld world;
    world.seed = seed;
    simulation::import_paths(world, *world_model);
    const auto guests = simulation::import_guests(world, *people);
    const std::size_t shops = simulation::import_stalls(world, *rides);
    std::vector<std::optional<simulation::RideTiming>> timings;
    std::vector<std::optional<simulation::Coaster>> coasters(rides->rides.size());
    std::set<std::uint64_t> sights;                        // Placed objects guests stop to watch
    std::set<std::uint64_t> gates;                         // Park entrances
    std::map<std::uint64_t, std::uint32_t> facility_kinds; // Special attractions' kinds, by attraction
    if (game)
    {
        const auto install = config::open_game_install(game);
        if (!install)
        {
            std::fprintf(stderr, "%s: not the game's folder\n", game);
            return 1;
        }
        // The folders the park's objects come from, as the game's folder spells them, and the overlays those
        // take assets from in turn.
        std::vector<std::string> folders;
        for (const save::PlacedObject& o : world_model->objects)
        {
            const std::string_view top = std::string_view(o.overlay).substr(0, o.overlay.find_first_of("/\\"));
            if (const auto found = top.empty() || top.size() == o.overlay.size() ? std::nullopt : install->find(top))
                if (std::ranges::find(folders, found->filename().string()) == folders.end())
                    folders.push_back(found->filename().string());
        }
        const std::vector<std::string_view> views(folders.begin(), folders.end());
        park::AssetIndex assets = park::AssetIndex::scan(install->root(), views);
        std::set<std::string, std::less<>> tried_files;
        for (auto missing = assets.missing_references(); !missing.empty(); missing = assets.missing_references())
        {
            bool added = false;
            for (const std::string& reference : missing)
                if (tried_files.insert(reference).second)
                    if (const auto file = install->find(reference + ".common.ovl"))
                        added = assets.add_file(*file) || added;
            if (!added)
                break;
        }
        park::ModelLoader loader(assets);
        for (const save::PlacedObject& o : world_model->objects)
            if (const auto model = loader.load(o.symbol, o.overlay); model && park::is_sight(model->category))
                sights.insert(o.id);
            else if (model && model->category == std::to_underlying(park::ItemCategory::ParkEntrance))
                gates.insert(o.id);
        for (const save::Ride& ride : rides->rides)
            if (ride.kind == save::RideKind::Special)
                if (const auto file = install->find(ride.base.overlay_path + ".common.ovl"))
                    if (const auto kind = park::special_attraction_kind(*file, std::string_view(ride.base.symbol).substr(0, ride.base.symbol.find(':'))))
                        facility_kinds[ride.base.id] = *kind;
        std::map<std::string, std::optional<park::ObjectModel>, std::less<>> models;
        timings = simulation::flat_ride_timings(*rides, world_model->objects, [&](const save::PlacedObject& o) -> const park::ObjectModel* {
            auto [at, added] = models.try_emplace(o.symbol);
            if (added)
                at->second = loader.load(o.symbol, o.overlay);
            return at->second ? &*at->second : nullptr;
        });
        // Each coaster's course, checked against where the save has its cars.
        park::ObjectModels piece_models;
        for (const save::PlacedObject& o : world_model->objects)
            if (!piece_models.contains(park::model_key(o)))
                if (auto model = loader.load(o.symbol, o.overlay))
                    piece_models.emplace(park::model_key(o), std::move(*model));
        std::map<std::string, std::optional<park::PieceCatalogue>, std::less<>> catalogues;
        for (std::size_t index = 0; index < rides->rides.size(); ++index)
        {
            const save::Ride& ride = rides->rides[index];
            const auto course = park::track_course(ride, world_model->objects, piece_models, world_model->terrain, { }, [&](const save::TrackPiece& piece, const save::PlacedObject& object) -> const park::PieceShape* {
                auto [at, added] = catalogues.try_emplace(object.overlay);
                if (added)
                    if (const auto file = install->find(object.overlay + ".common.ovl"))
                        if (auto loaded = park::load_piece_catalogue(*file, object.overlay))
                            at->second = std::move(*loaded);
                if (!at->second)
                    return nullptr;
                return park::shape_named(*at->second, piece.symbol);
            });
            if (!course)
                continue;
            float worst = 0;
            std::size_t cars = 0;
            for (const save::Train& train : ride.trains)
                for (const save::Car& car : train.cars)
                    if (car.position_valid)
                    {
                        worst = std::max(worst, glm::distance(park::course_point(*course, car.distance), glm::vec3 { car.position[0], car.position[1], car.position[2] }));
                        ++cars;
                    }
            std::printf("course %s closed %d: %.1f m (lap tested %.1f m), %zu points; %zu cars, farthest %.2f m off\n", ride.base.symbol.c_str(), course->closed, static_cast<double>(course->length()), static_cast<double>(ride.stats.last_lap ? ride.stats.last_lap->ride_length : 0), course->points.size(), cars, static_cast<double>(worst));
            coasters[index] = simulation::coaster_of(*course, ride);
            const auto lap = coasters[index] ? simulation::lap_seconds(*coasters[index]) : std::nullopt;
            if (ride.stats.last_lap)
            {
                const simulation::Ratings ours = simulation::rate(*ride.stats.last_lap);
                std::printf("  rated %.1f/%.1f/%.1f from its test, saved %.1f/%.1f/%.1f\n", static_cast<double>(ours.excitement), static_cast<double>(ours.intensity), static_cast<double>(ours.nausea), static_cast<double>(ride.assessment.excitement), static_cast<double>(ride.assessment.intensity), static_cast<double>(ride.assessment.nausea));
            }
            std::printf(
                "  lap %.1f s (tested %.1f s); train %.1f m mass %.0f friction %.3f / %.3f\n", static_cast<double>(lap.value_or(-1)), static_cast<double>(ride.stats.last_lap ? ride.stats.last_lap->ride_time : 0), static_cast<double>(coasters[index] ? coasters[index]->train_length : 0), static_cast<double>(ride.trains.empty() ? 0 : ride.trains[0].mass), static_cast<double>(ride.trains.empty() ? 0 : ride.trains[0].static_friction),
                static_cast<double>(ride.trains.empty() ? 0 : ride.trains[0].dynamic_friction));
        }
        for (std::size_t i = 0; i < timings.size(); ++i)
            if (timings[i])
                std::printf("flat ride %s: %u seats, %.1f s\n", rides->rides[i].base.symbol.c_str(), timings[i]->seats, static_cast<double>(timings[i]->seconds));
    }
    const auto ride_entities = simulation::import_rides(world, *rides, timings, coasters);
    const auto ride_count = static_cast<std::size_t>(std::ranges::count_if(ride_entities, [](entt::entity e) { return e != entt::null; }));
    {
        // How far the paths' heights are from where the save stands its guests, as a check on both.
        std::vector<float> gaps;
        for (const auto [e, pose] : world.registry.view<const simulation::Pose>().each())
            if (const auto at = world.paths.nearest(pose.position))
                gaps.push_back(std::abs(world.paths.centre(*at).y - pose.position.y));
        std::ranges::sort(gaps);
        if (!gaps.empty())
            std::printf("height gap to saved guests: median %.3f, 90%% %.3f, max %.3f\n", gaps[gaps.size() / 2], gaps[gaps.size() * 9 / 10], gaps.back());
    }
    const std::size_t walking = simulation::start_guests(world);
    const std::size_t seated = simulation::board_saved_riders(world, *people, guests, *rides, ride_entities);
    const std::size_t parties = simulation::import_parties(world, *people, guests);
    const auto park_model = save::load_park(*file, index);
    const std::size_t litter = simulation::import_litter(world, *people, simulation::park_state(park_model ? &*park_model : nullptr, *world_model).time);
    const std::size_t items = simulation::import_path_items(world, world_model->objects, world_model->bins, world_model->benches, world_model->signs);
    const auto staff = simulation::import_staff(world, *people);
    const std::size_t facilities = simulation::import_facilities(world, *rides, [&](const save::Ride& ride) -> std::optional<std::uint32_t> {
        const auto found = facility_kinds.find(ride.base.id);
        return found != facility_kinds.end() ? std::optional(found->second) : std::nullopt;
    });
    const std::size_t sight_count = simulation::import_sights(world, world_model->objects, [&](const save::PlacedObject& o) { return sights.contains(o.id); });
    const bool gate = simulation::import_gate(world, world_model->objects, [&](const save::PlacedObject& o) { return gates.contains(o.id); }) != entt::null;
    std::printf("facilities %zu  sights %zu  gate %s\n", facilities, sight_count, gate ? "yes" : "none");
    // The park's time of day, as its file left it, run through every hour.
    simulation::DayWindow hours;
    if (park_model && park_model->has_park)
        hours = simulation::opening_hours(park_model->park.opening_minutes, park_model->park.closing_minutes);
    simulation::DayClock day = simulation::start_day(world_model->time ? std::optional(world_model->time->day_fraction) : std::nullopt, simulation::day_window(simulation::day_mode(world_model->time ? world_model->time->day_night_mode : std::nullopt), hours), simulation::DayTransition::Instant);
    if (whole_day)
        day.window = { };
    {
        // How the coasters feel by day and in the dark, to teens and to families.
        float rated = 0;
        std::size_t coasters = 0;
        for (const auto [e, ride, trains] : world.registry.view<const simulation::CycleRide, const simulation::Trains>().each())
        {
            rated += ride.excitement;
            ++coasters;
        }
        if (coasters > 0)
        {
            using enum simulation::AgeGroup;
            rated /= static_cast<float>(coasters);
            std::printf("coasters' excitement: by day %.2f; at night %.2f to teens, %.2f to families\n", static_cast<double>(rated), static_cast<double>(simulation::felt_excitement(rated, true, Teen, true)), static_cast<double>(simulation::felt_excitement(rated, true, Adult, true)));
        }
    }
    world.registry.ctx().insert_or_assign(simulation::Daylight { day.time });
    // The park's own weather, unless another is asked for.
    auto& sky = world.registry.ctx().insert_or_assign(simulation::guest_weather(simulation::park_state(park_model ? &*park_model : nullptr, *world_model)));
    if (weather == "rain" || weather == "storm")
        sky = { .rain = true, .storm = weather == "storm", .temperature = 15 };
    else if (weather == "hot" || weather == "cold")
        sky = { .temperature = weather == "hot" ? 35.0f : 2.0f };
    std::printf("weather: %s%s, %.0f degrees\n", sky.rain ? "rain" : "dry", sky.storm ? " (storm)" : "", static_cast<double>(sky.temperature));
    const auto broken_rides = std::ranges::count_if(world.registry.view<const simulation::Upkeep>(), [&](entt::entity e) { return world.registry.get<simulation::Upkeep>(e).broken; });
    std::printf("litter %zu  path items %zu  rides broken down %td  staff on the paths %zu of %zu\n", litter, items, broken_rides, static_cast<std::size_t>(std::ranges::count_if(staff, [](entt::entity e) { return e != entt::null; })), staff.size());
    for (const auto& stall : rides->stalls)
        for (const auto& item : stall.items)
            std::printf("stall %s [%s] sells %s\n", stall.base.name.c_str(), stall.base.symbol.c_str(), item.name.c_str());
    std::printf("paths %zu  shops %zu  rides %zu  guests walking %zu  seated %zu  parties %zu\n", world.paths.nodes().size(), shops, ride_count, walking, seated, parties);
    {
        // Groups of path tiles joined to each other, to spot paths the import failed to join.
        const auto& nodes = world.paths.nodes();
        std::vector<std::size_t> group(nodes.size(), 0);
        std::vector<std::size_t> sizes;
        for (std::size_t s = 0; s < nodes.size(); ++s)
        {
            if (group[s] != 0)
                continue;
            sizes.push_back(0);
            std::vector<std::size_t> open { s };
            group[s] = sizes.size();
            while (!open.empty())
            {
                const std::size_t at = open.back();
                open.pop_back();
                ++sizes.back();
                for (const auto n : nodes[at].next)
                    if (n != simulation::kNoNode && group[n] == 0)
                    {
                        group[n] = sizes.size();
                        open.push_back(n);
                    }
            }
        }
        std::ranges::sort(sizes, std::greater { });
        std::printf("path groups %zu, largest", sizes.size());
        for (std::size_t i = 0; i < std::min<std::size_t>(sizes.size(), 8); ++i)
            std::printf(" %zu", sizes[i]);
        std::printf("\n");
    }

    const auto workers = threads > 0 ? std::make_unique<simulation::Workers>(threads) : nullptr;
    const std::uint32_t steps = simulation::ticks(seconds);
    const auto start = std::chrono::steady_clock::now();
    // Rides with more than one train: the closest any train came to the back of the one ahead, and how many
    // laps each train finished.
    struct Fleet
    {
        float closest = std::numeric_limits<float>::infinity();
        std::vector<std::uint32_t> laps;
        std::vector<float> travelled;
    };
    std::map<entt::entity, Fleet> fleets;
    for (const auto [e, trains] : world.registry.view<const simulation::Trains>().each())
        if (trains.trains.size() > 1)
            fleets[e] = { .laps = std::vector<std::uint32_t>(trains.trains.size()), .travelled = std::vector<float>(trains.trains.size()) };
    // Litter dropped and swept, bins emptied and things mended over the run.
    struct Tally
    {
        std::size_t dropped = 0, swept = 0, emptied = 0, mended = 0, repaired = 0, inspected = 0, sat = 0, watched = 0, sick = 0, sick_by_exit = 0;
        std::set<simulation::NodeId> exits;
        void linger(entt::registry& r, entt::entity e)
        {
            ++(r.get<simulation::Linger>(e).at == simulation::PathItemKind::Bench ? sat : watched);
        }
        void drop(entt::registry& r, entt::entity e)
        {
            ++dropped;
            if (const auto& piece = r.get<simulation::Litter>(e); piece.vomit)
            {
                ++sick;
                sick_by_exit += exits.contains(piece.node);
            }
        }
        void sweep(entt::registry&, entt::entity)
        {
            ++swept;
        }
    } tally;
    for (const auto [e, ride, attraction] : world.registry.view<const simulation::CycleRide, const simulation::Attraction>().each())
        tally.exits.insert(ride.exit != simulation::kNoNode ? ride.exit : attraction.entrance);
    world.registry.on_construct<simulation::Litter>().connect<&Tally::drop>(tally);
    world.registry.on_destroy<simulation::Litter>().connect<&Tally::sweep>(tally);
    world.registry.on_construct<simulation::Linger>().connect<&Tally::linger>(tally);
    std::map<entt::entity, std::pair<float, bool>> was;      // Each path item's bin contents and whether broken
    std::map<entt::entity, std::pair<float, bool>> ride_was; // Each ride's time since inspection and whether broken
    for (std::uint32_t i = 0; i < steps; ++i)
    {
        const auto hour = static_cast<int>(day.time * simulation::kHoursPerDay);
        simulation::advance(day, simulation::kTickSeconds);
        world.registry.ctx().insert_or_assign(simulation::Daylight { day.time });
        if (const auto now = static_cast<int>(day.time * simulation::kHoursPerDay); now != hour)
        {
            std::array<std::size_t, 3> in { };
            for (const auto [e, visitor] : world.registry.view<const simulation::Visitor>(entt::exclude<simulation::Left>).each())
                ++in[static_cast<std::size_t>(visitor.age)];
            const auto& turnstile = world.registry.ctx().emplace<simulation::Turnstile>();
            std::printf("%02d:00%s in the park: %zu children, %zu teens, %zu adults; came %u/%u/%u, went %u/%u/%u\n", now, simulation::is_night(day.time) ? " (dark)" : "", in[0], in[1], in[2], turnstile.in[0], turnstile.in[1], turnstile.in[2], turnstile.out[0], turnstile.out[1], turnstile.out[2]);
        }
        simulation::step(world, workers.get());
        for (const auto [e, item] : world.registry.view<const simulation::PathItem>().each())
        {
            const auto* bin = world.registry.try_get<simulation::Bin>(e);
            const auto [contents, broken] = was[e];
            tally.emptied += bin != nullptr && bin->contents < contents;
            tally.mended += broken && !item.broken;
            was[e] = { bin != nullptr ? bin->contents : 0.0f, item.broken };
        }
        for (const auto [e, upkeep] : world.registry.view<const simulation::Upkeep>().each())
        {
            const auto [since, broken] = ride_was.try_emplace(e, upkeep.since_inspection, upkeep.broken).first->second;
            tally.repaired += broken && !upkeep.broken;
            tally.inspected += !broken && upkeep.since_inspection < since;
            ride_was[e] = { upkeep.since_inspection, upkeep.broken };
        }
        for (auto& [e, fleet] : fleets)
        {
            const auto& trains = world.registry.get<simulation::Trains>(e).trains;
            for (std::size_t t = 0; t < trains.size(); ++t)
            {
                fleet.closest = std::min(fleet.closest, simulation::room_ahead(trains, t) + simulation::kBlockGap);
                fleet.laps[t] += trains[t].travelled < fleet.travelled[t];
                fleet.travelled[t] = trains[t].travelled;
            }
        }
    }
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("ran %u ticks (%.0f s of park time) in %.3f s, %.1f us a tick\n", steps, seconds, took, took * 1e6 / std::max(steps, 1u));

    auto& r = world.registry;
    for (const auto& [e, fleet] : fleets)
    {
        std::printf("  %-28s %zu trains, closest %.2f m, laps", r.get<simulation::Attraction>(e).name.c_str(), fleet.laps.size(), static_cast<double>(fleet.closest));
        for (const std::uint32_t d : fleet.laps)
            std::printf(" %u", d);
        std::printf("\n");
    }
    for (const auto [e, a] : r.view<const simulation::Attraction>().each())
    {
        const auto* shop = r.try_get<simulation::Shop>(e);
        constexpr std::array<const char*, 6> kProducts { "drink", "food", "toilet", "cash", "aid", "brolly" };
        const char* sells = !shop ? "ride" : kProducts[static_cast<std::size_t>(shop->product)];
        std::printf("  %-28s %-5s customers %5u  income %8.2f\n", a.name.c_str(), sells, a.customers, static_cast<double>(a.income) / 100);
    }
    {
        std::map<simulation::NodeId, std::size_t> lying;
        for (const auto [e, piece] : r.view<const simulation::Litter>().each())
            ++lying[piece.node];
        float held = 0;
        float room = 0;
        for (const auto [e, bin] : r.view<const simulation::Bin>().each())
        {
            held += bin.contents;
            room += bin.capacity;
        }
        const auto* vandalism = r.ctx().find<simulation::Vandalism>();
        std::printf("litter: %zu lying on %zu tiles (most %zu), %zu dropped, %zu swept; bins %.0f of %.0f full, emptied %zu times\n", r.view<const simulation::Litter>().size(), lying.size(), lying.empty() ? 0 : std::ranges::max(lying | std::views::values), tally.dropped, tally.swept, static_cast<double>(held), static_cast<double>(room), tally.emptied);
        std::printf("vandalism: %u broken, %zu mended, %u warnings; old litter for the rating %d\n", vandalism ? vandalism->broken : 0, tally.mended, vandalism ? vandalism->warnings : 0, simulation::old_litter(world));
        std::uint32_t breakdowns = 0;
        std::size_t down = 0;
        float reliability = 0;
        for (const auto [e, upkeep] : r.view<const simulation::Upkeep>().each())
        {
            breakdowns += upkeep.breakdowns;
            down += upkeep.broken;
            reliability += upkeep.reliability;
        }
        const auto ride_count = r.view<const simulation::Upkeep>().size();
        const std::size_t mechanics = static_cast<std::size_t>(std::ranges::count_if(r.view<const simulation::Staff>(), [&](entt::entity e) { return r.get<simulation::Staff>(e).role == simulation::StaffRole::Mechanic; }));
        const auto* payroll = r.ctx().find<simulation::Payroll>();
        std::map<simulation::ThoughtKind, std::size_t> thought;
        std::size_t thinkers = 0;
        for (const auto [e, thoughts] : r.view<const simulation::Thoughts>().each())
        {
            thinkers += !thoughts.recent.empty();
            for (const simulation::Thought& t : thoughts.recent)
                ++thought[t.kind];
        }
        std::size_t wet = 0;
        for (const auto [e, mood] : r.view<const simulation::Mood>().each())
            wet += mood.wet >= 0.5f;
        std::size_t queued[2] { };
        std::size_t rides[2] { };
        for (const auto [e, ride] : r.view<const simulation::CycleRide>().each())
        {
            queued[ride.cover >= 0.5f] += ride.queue.size();
            ++rides[ride.cover >= 0.5f];
        }
        std::uint32_t drinks = 0;
        for (const auto [e, shop, a] : r.view<const simulation::Shop, const simulation::Attraction>().each())
            drinks += shop.product == simulation::Product::Drink ? a.customers : 0;
        std::printf("weather: %zu wet, %zu with umbrellas; queueing for %zu open-air rides %zu, for %zu covered %zu; drinks sold %u\n", wet, r.view<const simulation::Umbrella>().size(), rides[0], queued[0], rides[1], queued[1], drinks);
        std::printf("thoughts held by %zu guests:", thinkers);
        for (const auto [kind, n] : thought)
            std::printf(" %d:%zu", std::to_underlying(kind), n);
        std::printf("\n");
        std::printf("guests sat on benches %zu times and stopped at sights %zu times\n", tally.sat, tally.watched);
        const auto vomit_lying = std::ranges::count_if(r.view<const simulation::Litter>(), [&](entt::entity e) { return r.get<simulation::Litter>(e).vomit; });
        const auto queasy = std::ranges::count_if(r.view<const simulation::Mood>(), [&](entt::entity e) { return r.get<simulation::Mood>(e).nausea >= 0.6f; });
        std::printf("sickness: guests sick %zu times, %zu by a ride's exit; %td lying now; %td guests too sick to ride\n", tally.sick, tally.sick_by_exit, vomit_lying, queasy);
        std::printf("rides: %u breakdowns, %zu repaired, %zu inspections, %zu down now, reliability %.2f on average; %zu mechanics; wages paid %.2f\n", breakdowns, tally.repaired, tally.inspected, down, static_cast<double>(ride_count > 0 ? reliability / static_cast<float>(ride_count) : 0), mechanics, static_cast<double>(payroll ? payroll->paid : 0) / 100);
    }
    std::array<std::size_t, 6> doing { };
    double happiness = 0;
    double thirst = 0;
    double hunger = 0;
    std::size_t count = 0;
    for (const auto [e, errand, mood, needs] : r.view<const simulation::Errand, const simulation::Mood, const simulation::Needs>().each())
    {
        ++doing[static_cast<std::size_t>(errand.kind)];
        happiness += mood.happiness;
        thirst += needs.thirst;
        hunger += needs.hunger;
        ++count;
    }
    if (count > 0)
        std::printf("guests: wandering %zu heading %zu queueing %zu riding %zu following %zu waiting %zu  happiness %.2f thirst %.2f hunger %.2f\n", doing[0], doing[1], doing[2], doing[3], doing[4], doing[5], happiness / static_cast<double>(count), thirst / static_cast<double>(count), hunger / static_cast<double>(count));
    // How well parties keep together: followers' distance from their leader, and how far waiting members stand
    // from the exit of the ride their party is on.
    std::vector<float> apart;
    std::vector<float> from_exit;
    for (const auto [e, errand, member, pose] : r.view<const simulation::Errand, const simulation::Member, const simulation::Pose>().each())
    {
        const auto& party = r.get<simulation::Party>(member.party);
        if (errand.kind == simulation::ErrandKind::Following)
            apart.push_back(glm::distance(pose.position, r.get<simulation::Pose>(party.leader).position));
        else if (errand.kind == simulation::ErrandKind::Waiting && r.valid(errand.target) && r.all_of<simulation::CycleRide>(errand.target) && r.get<simulation::Stride>(e).route.empty())
        {
            const simulation::NodeId exit = r.get<simulation::CycleRide>(errand.target).exit;
            if (exit != simulation::kNoNode)
                from_exit.push_back(glm::distance(pose.position, world.paths.centre(exit)));
        }
    }
    for (auto [name, v] : { std::pair { "followers from leader", &apart }, std::pair { "waiters from exit", &from_exit } })
    {
        if (v->empty())
            continue;
        std::ranges::sort(*v);
        std::printf("%s: %zu, median %.2f, 90%% %.2f, max %.2f\n", name, v->size(), static_cast<double>((*v)[v->size() / 2]), static_cast<double>((*v)[v->size() * 9 / 10]), static_cast<double>(v->back()));
    }
    return 0;
}
