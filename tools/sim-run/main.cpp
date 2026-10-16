// SPDX-License-Identifier: GPL-3.0-or-later
//
// sim-run: run the simulation headless on an original save and report what happened.
//
//   sim-run SAVE [--seconds N] [--threads N] [--seed N]
//
// Brings over the save's paths, stalls and guests, runs N seconds of park time (default 600) and prints
// what each shop took and how the guests are doing, with how long the run took.
#include "save/model/people.hpp"
#include "save/model/rides.hpp"
#include "save/model/world.hpp"
#include "save/save_file.hpp"
#include "simulation/components.hpp"
#include "simulation/import_park.hpp"
#include "simulation/import_people.hpp"
#include "simulation/systems.hpp"
#include "simulation/workers.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <vector>

int main(int argc, char** argv)
{
    using namespace rct3;
    const char* path = nullptr;
    float seconds = 600;
    unsigned threads = 0;
    std::uint64_t seed = 1;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc)
            seconds = std::strtof(argv[++i], nullptr);
        else if (std::strcmp(argv[i], "--threads") == 0 && i + 1 < argc)
            threads = static_cast<unsigned>(std::strtoul(argv[++i], nullptr, 10));
        else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
            seed = std::strtoull(argv[++i], nullptr, 10);
        else
            path = argv[i];
    }
    if (!path)
    {
        std::fprintf(stderr, "usage: sim-run SAVE [--seconds N] [--threads N] [--seed N]\n");
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
    const std::size_t walking = simulation::start_guests(world);
    for (const auto& stall : rides->stalls)
        for (const auto& item : stall.items)
            std::printf("stall %s sells %s\n", stall.base.name.c_str(), item.name.c_str());
    std::printf("paths %zu  shops %zu  guests walking %zu\n", world.paths.nodes().size(), shops, walking);
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
