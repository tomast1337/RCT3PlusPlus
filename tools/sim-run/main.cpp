// SPDX-License-Identifier: GPL-3.0-or-later
//
// sim-run: run the simulation headless on an original save and report what happened.
//
//   sim-run SAVE [--seconds N] [--threads N] [--seed N] [--game DIR]
//
// Brings over the save's paths, stalls, rides and guests, runs N seconds of park time (default 600) and prints
// what each shop and ride took and how the guests are doing, with how long the run took. Flat rides take
// their timing from their models, so they come over only with --game, the game's folder.
#include "config/game_install.hpp"
#include "park/asset_index.hpp"
#include "park/object_mesh.hpp"
#include "park/object_models.hpp"
#include "park/track_course.hpp"
#include "park/track_pieces.hpp"
#include "save/model/people.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"
#include "save/save_file.hpp"
#include "simulation/coaster.hpp"
#include "simulation/components.hpp"
#include "simulation/import_park.hpp"
#include "simulation/import_people.hpp"
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
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    using namespace rct3;
    const char* path = nullptr;
    float seconds = 600;
    unsigned threads = 0;
    std::uint64_t seed = 1;
    const char* game = nullptr;
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
        else
            path = argv[i];
    }
    if (!path)
    {
        std::fprintf(stderr, "usage: sim-run SAVE [--seconds N] [--threads N] [--seed N] [--game DIR]\n");
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
    simulation::import_guests(world, *people);
    const std::size_t shops = simulation::import_stalls(world, *rides);
    std::vector<std::optional<simulation::RideTiming>> timings;
    std::vector<std::optional<simulation::Coaster>> coasters(rides->rides.size());
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
    const auto ride_count = static_cast<std::size_t>(std::ranges::count_if(simulation::import_rides(world, *rides, timings, coasters), [](entt::entity e) { return e != entt::null; }));
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
    for (const auto& stall : rides->stalls)
        for (const auto& item : stall.items)
            std::printf("stall %s sells %s\n", stall.base.name.c_str(), item.name.c_str());
    std::printf("paths %zu  shops %zu  rides %zu  guests walking %zu\n", world.paths.nodes().size(), shops, ride_count, walking);
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
    for (std::uint32_t i = 0; i < steps; ++i)
        simulation::step(world, workers.get());
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("ran %u ticks (%.0f s of park time) in %.3f s, %.1f us a tick\n", steps, seconds, took, took * 1e6 / std::max(steps, 1u));

    auto& r = world.registry;
    for (const auto [e, a] : r.view<const simulation::Attraction>().each())
    {
        const auto* shop = r.try_get<simulation::Shop>(e);
        const char* sells = !shop ? "ride" : shop->product == simulation::Product::Drink ? "drink" : "food";
        std::printf("  %-28s %-5s customers %5u  income %8.2f\n", a.name.c_str(), sells, a.customers, static_cast<double>(a.income) / 100);
    }
    std::array<std::size_t, 4> doing { };
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
        std::printf("guests: wandering %zu heading %zu queueing %zu riding %zu  happiness %.2f thirst %.2f hunger %.2f\n", doing[0], doing[1], doing[2], doing[3], happiness / static_cast<double>(count), thirst / static_cast<double>(count), hunger / static_cast<double>(count));
    return 0;
}
