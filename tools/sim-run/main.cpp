// SPDX-License-Identifier: GPL-3.0-or-later
//
// sim-run: run the simulation headless on an original save and report what happened.
//
//   sim-run SAVE [--seconds N] [--threads N] [--seed N] [--game DIR] [--weather rain|storm|hot|cold] [--whole-day] [--patrol] [--profile] [--awards]
//
// Brings over the save's paths, stalls, rides and guests, runs N seconds of park time (default 600) and prints
// what each shop and ride took and how the guests are doing, with how long the run took. --profile adds the
// time each phase of a step took, as the profiler's table. --awards (with --game) adds how many rides and stalls
// carry each theme, as the theme awards count them. Flat rides take their timing from their models, so they come
// over only with --game, the game's folder.
#include "config/game_install.hpp"
#include "logging/crash.hpp"
#include "logging/log.hpp"
#include "park/asset_index.hpp"
#include "park/build_catalogue.hpp"
#include "park/item_category.hpp"
#include "park/object_mesh.hpp"
#include "park/object_models.hpp"
#include "park/overhead.hpp"
#include "park/track_course.hpp"
#include "park/track_pieces.hpp"
#include "profiling/profiler.hpp"
#include "save/model/park.hpp"
#include "save/model/people.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"
#include "save/save_file.hpp"
#include "simulation/awards.hpp"
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
#include <format>
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

namespace
{
    // Reports go out as plain lines, problems to standard error.
    const rct3::logging::Logger out = rct3::logging::logger("sim-run");
} // namespace

namespace
{
    /// How many rides are down with each kind of breakdown, as "kind:count ...".
    std::string breakdown_kinds(const entt::registry& r)
    {
        std::map<int, std::size_t> kinds;
        for (const auto [_, upkeep] : r.view<const rct3::simulation::Upkeep>().each())
            if (upkeep.broken)
                ++kinds[std::to_underlying(upkeep.kind)];
        std::string out;
        for (const auto [kind, n] : kinds)
            out += std::format(" {}:{}", kind, n);
        return out.empty() ? " none" : out;
    }
} // namespace

int main(int argc, char** argv)
{
    rct3::logging::configure({ .memory_capacity = 0, .plain = true });
    using namespace rct3;
    logging::install_crash_report();
    const char* path = nullptr;
    float seconds = 600;
    unsigned threads = 0;
    std::uint64_t seed = 1;
    const char* game = nullptr;
    std::string_view weather;
    bool whole_day = false; // Run the clock through the night, whatever the park's day mode
    bool patrol = false;    // Give each handyman and mechanic a patrol: the tiles round where they start
    bool profile = false;   // Print the time each phase of a step took
    bool awards = false;    // Print what the theme awards count
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
        else if (std::strcmp(argv[i], "--patrol") == 0)
            patrol = true;
        else if (std::strcmp(argv[i], "--profile") == 0)
            profile = true;
        else if (std::strcmp(argv[i], "--awards") == 0)
            awards = true;
        else if (std::strcmp(argv[i], "--weather") == 0 && i + 1 < argc)
            weather = argv[++i];
        else
            path = argv[i];
    }
    if (!path)
    {
        out.error("usage: sim-run SAVE [--seconds N] [--threads N] [--seed N] [--game DIR] [--weather rain|storm|hot|cold] [--whole-day] [--patrol] [--profile] [--awards]");
        return 2;
    }
    std::ifstream in(path, std::ios::binary);
    const std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!in && raw.empty())
    {
        out.error("{}: cannot read", path);
        return 1;
    }
    const auto file = save::read_save(std::as_bytes(std::span(raw)), { .verify_checksum = false });
    if (!file)
    {
        out.error("{}: {}", path, save::to_string(file.error()));
        return 1;
    }
    const save::ObjectIndex index(*file);
    const auto people = save::load_people(*file, index);
    const auto world_model = save::load_world(*file, index);
    const auto rides = save::load_rides(*file, index);
    if (!people || !world_model || !rides)
    {
        out.error("{}: cannot load people, world or rides", path);
        return 1;
    }

    simulation::SimWorld world;
    world.seed = rct3::random::Seed { seed };
    simulation::import_paths(world, *world_model);
    const auto guests = simulation::import_guests(world, *people);
    const std::size_t shops = simulation::import_stalls(world, *rides);
    std::vector<std::optional<simulation::RideTiming>> timings;
    std::vector<std::optional<simulation::Coaster>> coasters(rides->rides.size());
    std::vector<std::optional<park::TrackCourse>> courses(rides->rides.size());
    std::vector<std::optional<std::uint32_t>> guests_room;
    std::map<std::uint64_t, simulation::PathItemKind> sights;   // Placed objects guests stop at
    std::map<std::uint64_t, std::array<std::int32_t, 2>> gates; // Park entrances, with the tiles each covers
    std::map<std::uint64_t, std::uint32_t> facility_kinds;      // Special attractions' kinds, by attraction
    if (game)
    {
        const auto install = config::open_game_install(game);
        if (!install)
        {
            out.error("{}: not the game's folder", game);
            return 1;
        }
        if (awards)
        {
            std::string line;
            for (const auto& [theme, n] : simulation::themed_attractions(*rides, world_model->objects, park::read_build_catalogue(install->root())))
                line += std::format(" {} {:d}", theme, n);
            out.info("themes: rides and stalls carrying each:{}", line.empty() ? " none" : line);
        }
        // The folders the park's objects come from, as the game's folder spells them, and the overlays those
        // take assets from in turn.
        std::vector<std::string> folders;
        for (const save::PlacedObject& o : world_model->objects)
        {
            const std::string_view top = std::string_view(o.overlay).substr(0, o.overlay.find_first_of("/\\"));
            if (const auto found = top.empty() || top.size() == o.overlay.size() ? std::nullopt : install->find(top))
                if (!std::ranges::contains(folders, found->filename().string()))
                    folders.push_back(found->filename().string());
        }
        const auto views = folders | std::ranges::to<std::vector<std::string_view>>();
        park::AssetIndex assets = park::AssetIndex::scan(install->root(), views);
        std::set<std::string, std::less<>> tried_files;
        for (auto missing = assets.missing_references(); !missing.empty(); missing = assets.missing_references())
        {
            bool added = false;
            for (const std::string& reference : missing)
                if (tried_files.insert(reference).second)
                    if (const auto file = install->find(std::format("{}.common.ovl", reference)))
                        added = assets.add_file(*file) || added;
            if (!added)
                break;
        }
        park::ModelLoader loader(assets);
        for (const save::PlacedObject& o : world_model->objects)
            if (const auto model = loader.load(o.symbol, o.overlay); model && park::is_sight(model->category))
                sights.emplace(o.id, model->photo_spot ? simulation::PathItemKind::PhotoSpot : simulation::PathItemKind::Sight);
            else if (model && model->category == std::to_underlying(park::ItemCategory::ParkEntrance))
                gates.emplace(o.id, model->squares);
        for (const save::Ride& ride : rides->rides)
            if (ride.kind == save::RideKind::Special)
                if (const auto file = install->find(std::format("{}.common.ovl", ride.base.overlay_path)))
                    if (const auto kind = park::special_attraction_kind(*file, std::string_view(ride.base.symbol).substr(0, ride.base.symbol.find(':'))))
                        facility_kinds[ride.base.id] = *kind;
        std::map<std::string, std::optional<park::ObjectModel>, std::less<>> models;
        guests_room = simulation::ride_guests(*rides, [&](std::string_view path) { return install->find(path); });
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
        const std::size_t roofed = simulation::import_cover(world, [&](std::span<const glm::vec3> points) { return park::overheads(points, world_model->objects, piece_models, world_model->terrain, { }); });
        out.info("paths under a roof {:d}, in the shade {:d} of {:d}", roofed, static_cast<std::size_t>(std::ranges::count_if(world.paths.nodes(), &simulation::PathNode::shaded)), world.paths.nodes().size());
        std::map<std::string, std::optional<park::PieceCatalogue>, std::less<>> catalogues;
        for (std::size_t index = 0; index < rides->rides.size(); ++index)
        {
            const save::Ride& ride = rides->rides[index];
            const auto course = park::track_course(ride, world_model->objects, piece_models, world_model->terrain, { }, [&](const save::TrackPiece& piece, const save::PlacedObject& object) -> std::optional<const park::PieceShape&> {
                auto [at, added] = catalogues.try_emplace(object.overlay);
                if (added)
                    if (const auto file = install->find(std::format("{}.common.ovl", object.overlay)))
                        if (auto loaded = park::load_piece_catalogue(*file, object.overlay))
                            at->second = std::move(*loaded);
                if (!at->second)
                    return std::nullopt;
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
            out.info("course {} closed {:d}: {:.1f} m (lap tested {:.1f} m), {:d} points; {:d} cars, farthest {:.2f} m off", ride.base.symbol.c_str(), course->closed, static_cast<double>(course->length()), static_cast<double>(ride.stats.last_lap ? ride.stats.last_lap->ride_length : 0), course->points.size(), cars, static_cast<double>(worst));
            coasters[index] = simulation::coaster_of(*course, ride);
            courses[index] = *course;
            const auto lap = coasters[index] ? simulation::lap_seconds(*coasters[index]) : std::nullopt;
            if (ride.stats.last_lap)
            {
                const simulation::Ratings ours = simulation::rate(simulation::builtin_rating_model(), ride.base.symbol, *ride.stats.last_lap);
                out.info("  rated {:.1f}/{:.1f}/{:.1f} from its test, saved {:.1f}/{:.1f}/{:.1f}", static_cast<double>(ours.excitement), static_cast<double>(ours.intensity), static_cast<double>(ours.nausea), static_cast<double>(ride.assessment.excitement), static_cast<double>(ride.assessment.intensity), static_cast<double>(ride.assessment.nausea));
            }
            out.info(
                "  lap {:.1f} s (tested {:.1f} s); train {:.1f} m mass {:.0f} friction {:.3f} / {:.3f}", static_cast<double>(lap.value_or(-1)), static_cast<double>(ride.stats.last_lap ? ride.stats.last_lap->ride_time : 0), static_cast<double>(coasters[index] ? coasters[index]->train_length : 0), static_cast<double>(ride.trains.empty() ? 0 : ride.trains[0].mass), static_cast<double>(ride.trains.empty() ? 0 : ride.trains[0].static_friction),
                static_cast<double>(ride.trains.empty() ? 0 : ride.trains[0].dynamic_friction));
        }
        for (std::size_t i = 0; i < timings.size(); ++i)
            if (timings[i])
                out.info("flat ride {}: {:d} seats, {:.1f} s", rides->rides[i].base.symbol.c_str(), timings[i]->seats, static_cast<double>(timings[i]->start + static_cast<float>(timings[i]->loops) * timings[i]->run + timings[i]->stop));
    }
    const auto ride_entities = simulation::import_rides(world, *rides, timings, coasters, guests_room);
    simulation::import_boats(world, *rides, ride_entities, courses, world_model->terrain, world_model->water.bodies);
    simulation::link_stations(world, ride_entities, courses, world_model->terrain);
    const auto ride_count = static_cast<std::size_t>(std::ranges::count_if(ride_entities, [](entt::entity e) { return e != entt::null; }));
    {
        // How far the paths' heights are from where the save stands its guests, as a check on both.
        std::vector<float> gaps;
        for (const auto [_, pose] : world.registry.view<const simulation::Pose>().each())
            if (const auto at = world.paths.nearest(pose.position))
                gaps.push_back(std::abs(world.paths.centre(*at).y - pose.position.y));
        std::ranges::sort(gaps);
        if (!gaps.empty())
            out.info("height gap to saved guests: median {:.3f}, 90% {:.3f}, max {:.3f}", gaps[gaps.size() / 2], gaps[gaps.size() * 9 / 10], gaps.back());
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
    const std::size_t sight_count = simulation::import_sights(world, world_model->objects, [&](const save::PlacedObject& o) {
        const auto found = sights.find(o.id);
        return found != sights.end() ? std::optional(found->second) : std::nullopt;
    });
    const bool gate = simulation::import_gate(
                          world, world_model->objects,
                          [&](const save::PlacedObject& o) {
                              const auto found = gates.find(o.id);
                              return found != gates.end() ? std::optional(found->second) : std::nullopt;
                          })
        != entt::null;
    simulation::keep_staff_in(world, world_model->terrain);
    if (park_model && park_model->group_mix)
        simulation::import_party_mix(world, *park_model->group_mix);
    out.info("facilities {:d}  sights {:d}  gate {}", facilities, sight_count, gate ? "yes" : "none");
    // The park's time of day, as its file left it, run through every hour.
    simulation::DayWindow hours;
    if (park_model && park_model->has_park)
        hours = simulation::opening_hours(park_model->park.opening_minutes, park_model->park.closing_minutes);
    simulation::DayClock day = simulation::start_day(world_model->time ? std::optional(world_model->time->day_fraction) : std::nullopt, simulation::day_window(simulation::day_mode(world_model->time ? world_model->time->day_night_mode : std::nullopt), hours), simulation::DayTransition::Instant);
    if (whole_day)
        day.window = { };
    std::size_t patrolled = 0;
    std::size_t strays = 0; // Ticks a patrolled staff member walked outside their area on no job and not on the way back
    if (patrol)
        for (const auto [e, staff, stride] : world.registry.view<const simulation::Staff, const simulation::Stride>().each())
        {
            if ((staff.role != simulation::StaffRole::Handyman && staff.role != simulation::StaffRole::Mechanic) || stride.to >= world.paths.nodes().size())
                continue;
            const simulation::PathTileSpot at = world.paths.nodes()[stride.to].tile;
            simulation::Patrol area;
            for (int col = -4; col <= 4; ++col)
                for (int row = -4; row <= 4; ++row)
                    area.tiles.emplace_back(at.col + col, at.row + row);
            world.registry.emplace<simulation::Patrol>(e, std::move(area));
            ++patrolled;
        }
    {
        // How the coasters feel by day and in the dark, to teens and to families.
        float rated = 0;
        std::size_t coasters = 0;
        for (const auto [e, ride, trains] : world.registry.view<const simulation::CycleRide, const simulation::Trains>().each())
        {
            rated += ride.excitement.value;
            ++coasters;
        }
        if (coasters > 0)
        {
            using enum simulation::AgeGroup;
            rated /= static_cast<float>(coasters);
            out.info("coasters' excitement: by day {:.2f}; at night {:.2f} to teens, {:.2f} to families", static_cast<double>(rated), static_cast<double>(simulation::felt_excitement(rated, true, Teen, true)), static_cast<double>(simulation::felt_excitement(rated, true, Adult, true)));
        }
    }
    world.registry.ctx().insert_or_assign(simulation::Daylight { day.time });
    // The park's own weather, unless another is asked for.
    const simulation::ParkState state = simulation::park_state(park_model ? &*park_model : nullptr, *world_model);
    auto& sky = world.registry.ctx().insert_or_assign(simulation::guest_weather(state));
    const std::int64_t fee = park_model && park_model->has_finance ? park_model->finance.entrance_fee : 0;
    const auto& draw = world.registry.ctx().insert_or_assign(simulation::Draw { .entrance_fee = { fee }, .pool_squares = static_cast<std::uint32_t>(world_model->pool_water.size()), .rating = state.rating });
    if (weather == "rain" || weather == "storm")
        sky = { .rain = true, .storm = weather == "storm", .temperature = 15 };
    else if (weather == "hot" || weather == "cold")
        sky = { .sunny = weather == "hot", .temperature = weather == "hot" ? 35.0f : 2.0f };
    out.info("weather: {}{}{}, {:.0f} degrees; {:.1f} guests an hour come in", sky.rain ? "rain" : "dry", sky.storm ? " (storm)" : "", sky.sunny ? ", sunny" : "", static_cast<double>(sky.temperature.value), simulation::arrivals_per_second(sky) * 3600);
    const std::uint32_t ideal = simulation::ideal_guests(world);
    const std::int64_t fair = simulation::fair_entrance_fee(world);
    out.info("room for {:d} guests; entrance fee {:d} against {:d} fair; arrivals x{:.2f} with the park empty", ideal, static_cast<long long>(fee), static_cast<long long>(fair), simulation::drawn(draw, 0, ideal, fair));
    const auto broken_rides = std::ranges::count_if(world.registry.view<const simulation::Upkeep>(), [&](entt::entity e) { return world.registry.get<simulation::Upkeep>(e).broken; });
    out.info("breakdowns going on, by kind:{}", breakdown_kinds(world.registry).c_str());
    out.info("litter {:d}  path items {:d}  rides broken down {:d}  staff on the paths {:d} of {:d}", litter, items, broken_rides, static_cast<std::size_t>(std::ranges::count_if(staff, [](entt::entity e) { return e != entt::null; })), staff.size());
    for (const auto& stall : rides->stalls)
        for (const auto& item : stall.items)
            out.info("stall {} [{}] sells {} ({})", stall.base.name.c_str(), stall.base.symbol.c_str(), item.name.c_str(), item.kind_symbol.c_str());
    out.info("paths {:d}  shops {:d}  rides {:d}  guests walking {:d}  seated {:d}  parties {:d}", world.paths.nodes().size(), shops, ride_count, walking, seated, parties);
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
        std::string line = std::format("path groups {}, largest", sizes.size());
        for (std::size_t i = 0; i < std::min<std::size_t>(sizes.size(), 8); ++i)
            line += std::format(" {}", sizes[i]);
        out.info("{}", line);
    }

    const auto workers = threads > 0 ? std::make_unique<simulation::Workers>(threads) : nullptr;
    const std::uint32_t steps = simulation::ticks(seconds);
    if (profile)
        profiling::register_thread("Main");
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
    std::size_t squares = 0;                                 // Diagonals across squares of paths, each counted from both ends
    for (const simulation::PathNode& node : world.paths.nodes())
        squares += static_cast<std::size_t>(std::ranges::count_if(node.corner, [](simulation::NodeId c) { return c != simulation::kNoNode; }));
    std::size_t stepping = 0;
    std::size_t across = 0; // Of the guests' steps between tiles, tick by tick, those on a diagonal
    for (std::uint32_t i = 0; i < steps; ++i)
    {
        for (const auto [g, stride, needs] : world.registry.view<const simulation::Stride, const simulation::Needs>().each())
        {
            if (stride.from == stride.to || stride.from >= world.paths.nodes().size())
                continue;
            ++stepping;
            across += std::ranges::contains(world.paths.nodes()[stride.from].corner, stride.to);
        }
        for (const auto [e, staff, stride, area] : world.registry.view<const simulation::Staff, const simulation::Stride, const simulation::Patrol>().each())
            strays += staff.ride == entt::null && stride.route.empty() && stride.from != stride.to && !area.covers(world.paths.nodes()[stride.to].tile);
        const auto hour = static_cast<int>(day.time * simulation::kHoursPerDay);
        simulation::advance(day, simulation::kTickSeconds);
        world.registry.ctx().insert_or_assign(simulation::Daylight { day.time });
        if (const auto now = static_cast<int>(day.time * simulation::kHoursPerDay); now != hour)
        {
            std::array<std::size_t, 3> in { };
            for (const auto [_, visitor] : world.registry.view<const simulation::Visitor>(entt::exclude<simulation::Left>).each())
                ++in[static_cast<std::size_t>(visitor.age)];
            const auto& turnstile = world.registry.ctx().emplace<simulation::Turnstile>();
            out.info("{:02d}:00{} in the park: {:d} children, {:d} teens, {:d} adults; came {:d}/{:d}/{:d}, went {:d}/{:d}/{:d}", now, simulation::is_night(day.time) ? " (dark)" : "", in[0], in[1], in[2], turnstile.in[0], turnstile.in[1], turnstile.in[2], turnstile.out[0], turnstile.out[1], turnstile.out[2]);
        }
        profiling::section("Step", [&] { simulation::step(world, maybe(workers.get())); });
        profiling::capture_frame();
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
            const auto [since, broken] = ride_was.try_emplace(e, upkeep.since_inspection.value, upkeep.broken).first->second;
            tally.repaired += broken && !upkeep.broken;
            tally.inspected += !broken && upkeep.since_inspection.value < since;
            ride_was[e] = { upkeep.since_inspection.value, upkeep.broken };
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
    out.info("ran {:d} ticks ({:.0f} s of park time) in {:.3f} s, {:.1f} us a tick", steps, seconds, took, took * 1e6 / std::max(steps, 1u));
    if (profile)
    {
        std::string tsv = profiling::tsv_snapshot();
        if (tsv.ends_with('\n'))
            tsv.pop_back(); // The log ends the line
        out.info("{}", tsv);
    }

    auto& r = world.registry;
    for (const auto& [e, fleet] : fleets)
    {
        std::string line = std::format("  {:<28} {} trains, closest {:.2f} m, laps", r.get<simulation::Attraction>(e).name, fleet.laps.size(), fleet.closest);
        for (const std::uint32_t d : fleet.laps)
            line += std::format(" {}", d);
        out.info("{}", line);
    }
    for (const auto [e, a] : r.view<const simulation::Attraction>().each())
    {
        const auto* shop = r.try_get<simulation::Shop>(e);
        constexpr std::array<const char*, 6> kProducts { "drink", "food", "toilet", "cash", "aid", "brolly" };
        const char* sells = !shop ? "ride" : kProducts[static_cast<std::size_t>(shop->product)];
        std::size_t suits = 0; // Guests whose taste takes in a ride's intensity
        const auto* ride = r.try_get<simulation::CycleRide>(e);
        if (ride)
            for (const auto [_, taste] : r.view<const simulation::Taste>(entt::exclude<simulation::Left>).each())
                suits += taste.least.value <= ride->intensity.value && ride->intensity.value <= taste.most.value;
        std::string line = std::format("  {:<28} {:<6} customers {:5}  income {:8.2f}  suits {}", a.name, sells, a.customers, static_cast<double>(a.income.value) / 100, suits);
        if (ride)
        {
            line += std::format("  queue {} on {} tiles{}", ride->queue.size(), ride->line.size(), a.open ? "" : "  closed");
            // On a ride with more than one station: by station, those queueing, and those riding from and to it.
            if (!ride->stops.empty())
            {
                std::vector<std::array<std::size_t, 3>> at(1 + ride->stops.size());
                for (std::size_t s = 0; s < at.size(); ++s)
                    at[s][0] = s == 0 ? ride->queue.size() : ride->stops[s - 1].queue.size();
                for (const auto [_, errand] : r.view<const simulation::Errand>().each())
                    if (errand.target == e && errand.kind == simulation::ErrandKind::Riding && errand.station < at.size() && errand.off < at.size())
                        ++at[errand.station][1], ++at[errand.off][2];
                for (std::size_t s = 0; s < at.size(); ++s)
                    line += std::format("  [station {} queue {} on {} off {}]", s, at[s][0], at[s][1], at[s][2]);
            }
        }
        out.info("{}", line);
    }
    {
        std::map<simulation::NodeId, std::size_t> lying;
        for (const auto [_, piece] : r.view<const simulation::Litter>().each())
            ++lying[piece.node];
        float held = 0;
        float room = 0;
        for (const auto [_, bin] : r.view<const simulation::Bin>().each())
        {
            held += bin.contents;
            room += bin.capacity;
        }
        const auto* vandalism = r.ctx().find<simulation::Vandalism>();
        out.info("litter: {:d} lying on {:d} tiles (most {:d}), {:d} dropped, {:d} swept; bins {:.0f} of {:.0f} full, emptied {:d} times", r.view<const simulation::Litter>().size(), lying.size(), lying.empty() ? 0 : std::ranges::max(lying | std::views::values), tally.dropped, tally.swept, static_cast<double>(held), static_cast<double>(room), tally.emptied);
        out.info("vandalism: {:d} broken, {:d} mended, {:d} warnings; old litter for the rating {:d}", vandalism ? vandalism->broken : 0, tally.mended, vandalism ? vandalism->warnings : 0, simulation::old_litter(world));
        std::uint32_t breakdowns = 0;
        std::size_t down = 0;
        float reliability = 0;
        for (const auto [_, upkeep] : r.view<const simulation::Upkeep>().each())
        {
            breakdowns += upkeep.breakdowns;
            down += upkeep.broken;
            reliability += upkeep.reliability.value;
        }
        const auto ride_count = r.view<const simulation::Upkeep>().size();
        const std::size_t mechanics = static_cast<std::size_t>(std::ranges::count_if(r.view<const simulation::Staff>(), [&](entt::entity e) { return r.get<simulation::Staff>(e).role == simulation::StaffRole::Mechanic; }));
        const auto* payroll = r.ctx().find<simulation::Payroll>();
        std::map<simulation::ThoughtKind, std::size_t> thought;
        std::size_t thinkers = 0;
        for (const auto [_, thoughts] : r.view<const simulation::Thoughts>().each())
        {
            thinkers += !thoughts.recent.empty();
            for (const simulation::Thought& t : thoughts.recent)
                ++thought[t.kind];
        }
        std::size_t wet = 0;
        std::size_t hot = 0;
        for (const auto [_, mood] : r.view<const simulation::Mood>().each())
        {
            wet += mood.wet.value >= 0.5f;
            hot += mood.heat.value >= 0.5f;
        }
        // Walking guests by what is over the tile they are on.
        std::size_t roofed = 0;
        std::size_t shaded = 0;
        for (const auto [e, errand, stride] : r.view<const simulation::Errand, const simulation::Stride>().each())
            if (errand.kind != simulation::ErrandKind::Riding && errand.kind != simulation::ErrandKind::Queueing && stride.from < world.paths.nodes().size())
            {
                roofed += world.paths.nodes()[stride.from].covered;
                shaded += world.paths.nodes()[stride.from].shaded;
            }
        std::size_t queued[2] { };
        std::size_t rides[2] { };
        for (const auto [_, ride] : r.view<const simulation::CycleRide>().each())
        {
            queued[ride.cover.value >= 0.5f] += ride.queue.size();
            ++rides[ride.cover.value >= 0.5f];
        }
        std::uint32_t drinks = 0;
        std::uint32_t umbrellas = 0;
        std::array<std::uint32_t, 3> served { }; // Food and drink sold, by Serve
        for (const auto [e, shop, a] : r.view<const simulation::Shop, const simulation::Attraction>().each())
        {
            drinks += shop.product == simulation::Product::Drink ? a.customers : 0;
            umbrellas += shop.product == simulation::Product::Umbrella ? a.customers : 0;
            if (shop.product == simulation::Product::Drink || shop.product == simulation::Product::Food)
                served[std::to_underlying(shop.serve)] += a.customers;
        }
        out.info("weather: {:d} wet, {:d} too hot, {:d} with umbrellas ({:d} sold); walking under a roof {:d}, in the shade {:d}; queueing for {:d} open-air rides {:d}, for {:d} covered {:d}; drinks sold {:d}; food and drink sold plain {:d}, cold {:d}, hot {:d}", wet, hot, r.view<const simulation::Umbrella>().size(), umbrellas, roofed, shaded, rides[0], queued[0], rides[1], queued[1], drinks, served[0], served[1], served[2]);
        {
            // Parties waiting by rides: how many stand on an exit tile, and how many share a spot.
            std::size_t waiting = 0;
            std::size_t on_exits = 0;
            std::map<std::pair<int, int>, std::size_t> spots; // Waiting guests by where they stand, to a tenth of a unit
            for (const auto [g, errand, stride, pose] : r.view<const simulation::Errand, const simulation::Stride, const simulation::Pose>().each())
            {
                if (errand.kind != simulation::ErrandKind::Waiting || !stride.route.empty())
                    continue;
                ++waiting;
                on_exits += tally.exits.contains(stride.to);
                ++spots[{ static_cast<int>(pose.position.x * 10), static_cast<int>(pose.position.z * 10) }];
            }
            const auto* boarding = r.ctx().find<simulation::PartyBoarding>();
            out.info("parties: held back to ride together {:d}, split over two loads {:d}; waiting by rides {:d}, on exit tiles {:d}, most on one spot {:d}", boarding ? boarding->held : 0, boarding ? boarding->split : 0, waiting, on_exits, spots.empty() ? 0 : std::ranges::max(spots | std::views::values));
        }
        out.info("paths: {:d} diagonals across squares; {:.1f}% of guests' walking on them", squares / 2, stepping > 0 ? 100.0 * static_cast<double>(across) / static_cast<double>(stepping) : 0.0);
        std::string line = std::format("thoughts held by {} guests:", thinkers);
        for (const auto [kind, n] : thought)
            line += std::format(" {:d}:{}", std::to_underlying(kind), n);
        out.info("{}", line);
        out.info("guests sat on benches {:d} times and stopped at sights {:d} times", tally.sat, tally.watched);
        const auto vomit_lying = std::ranges::count_if(r.view<const simulation::Litter>(), [&](entt::entity e) { return r.get<simulation::Litter>(e).vomit; });
        const auto queasy = std::ranges::count_if(r.view<const simulation::Mood>(), [&](entt::entity e) { return r.get<simulation::Mood>(e).nausea.value >= 0.6f; });
        out.info("sickness: guests sick {:d} times, {:d} by a ride's exit; {:d} lying now; {:d} guests too sick to ride", tally.sick, tally.sick_by_exit, vomit_lying, queasy);
        out.info("breakdowns going on, by kind:{}", breakdown_kinds(r).c_str());
        {
            float total = 0;
            float worst = 0;
            std::size_t counted = 0;
            for (const auto [_, upkeep] : r.view<const simulation::Upkeep>().each())
            {
                total += upkeep.downtime_share();
                worst = std::max(worst, upkeep.downtime_share());
                ++counted;
            }
            out.info("downtime: {:.1f}% on average, {:.1f}% at worst", counted > 0 ? 100.0 * total / static_cast<double>(counted) : 0.0, 100.0 * static_cast<double>(worst));
        }
        if (patrol)
        {
            // Litter lying on the handymen's patrols and off them.
            std::size_t on = 0;
            std::size_t off = 0;
            const auto areas = r.view<const simulation::Staff, const simulation::Patrol>();
            for (const auto [_, litter] : r.view<const simulation::Litter>().each())
            {
                const simulation::PathTileSpot at = world.paths.nodes()[litter.node].tile;
                const bool covered = std::ranges::any_of(areas, [&](entt::entity s) { return areas.get<const simulation::Staff>(s).role == simulation::StaffRole::Handyman && areas.get<const simulation::Patrol>(s).covers(at); });
                ++(covered ? on : off);
            }
            std::size_t away = 0; // Patrolled staff outside their area now
            for (const auto [s, stride, area] : r.view<const simulation::Stride, const simulation::Patrol>().each())
                away += !area.covers(world.paths.nodes()[stride.to].tile);
            out.info("patrols: {:d} staff on 9x9 areas; {:d} ticks walking outside on no job; litter {:d} on handymen's areas, {:d} off them; {:d} outside their area now; paths no handyman patrols {:d}, no mechanic {:d}", patrolled, strays, on, off, away, simulation::uncovered_paths(world, simulation::StaffRole::Handyman), simulation::uncovered_paths(world, simulation::StaffRole::Mechanic));
        }
        out.info("rides: {:d} breakdowns, {:d} repaired, {:d} inspections, {:d} down now, reliability {:.2f} on average; {:d} mechanics; wages paid {:.2f}", breakdowns, tally.repaired, tally.inspected, down, static_cast<double>(ride_count > 0 ? reliability / static_cast<float>(ride_count) : 0), mechanics, static_cast<double>(payroll ? payroll->paid.value : 0) / 100);
        {
            float happy = 0, lazy = 0, load = 0;
            std::size_t count = 0, resting = 0;
            for (const auto [_, staff] : world.registry.view<const simulation::Staff>().each())
            {
                happy += staff.happiness.value;
                lazy += staff.laziness.value;
                load += staff.load.value;
                resting += staff.resting;
                ++count;
            }
            const auto mean = [&](float sum) { return static_cast<double>(count > 0 ? sum / static_cast<float>(count) : 0); };
            out.info("staff: {:d}, happiness {:.2f}, laziness {:.2f}, at work {:.2f} of the time on average; {:d} resting now", count, mean(happy), mean(lazy), mean(load), resting);
        }
    }
    std::array<std::size_t, 6> doing { };
    double happiness = 0;
    double thirst = 0;
    double hunger = 0;
    std::size_t count = 0;
    for (const auto [e, errand, mood, needs] : r.view<const simulation::Errand, const simulation::Mood, const simulation::Needs>().each())
    {
        ++doing[static_cast<std::size_t>(errand.kind)];
        happiness += mood.happiness.value;
        thirst += needs.thirst.value;
        hunger += needs.hunger.value;
        ++count;
    }
    if (count > 0)
        out.info("guests: wandering {:d} heading {:d} queueing {:d} riding {:d} following {:d} waiting {:d}  happiness {:.2f} thirst {:.2f} hunger {:.2f}", doing[0], doing[1], doing[2], doing[3], doing[4], doing[5], happiness / static_cast<double>(count), thirst / static_cast<double>(count), hunger / static_cast<double>(count));
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
        out.info("{}: {:d}, median {:.2f}, 90% {:.2f}, max {:.2f}", name, v->size(), static_cast<double>((*v)[v->size() / 2]), static_cast<double>((*v)[v->size() * 9 / 10]), static_cast<double>(v->back()));
    }
    return 0;
}
