// SPDX-License-Identifier: GPL-3.0-or-later
//
// park-dump: show a park file as JSON, or make one from an original save.
//
//   park-dump PARK                      print our park file as JSON
//   park-dump --import SAVE [--write PARK]
//                                       bring an original save's simulated parts over, print them as JSON,
//                                       and with --write also store them as our park file
//
// Exit status is 0 when everything read and wrote.
#include "nlohmann/json.hpp"
#include "parkfile/park_file.hpp"
#include "save/model/people.hpp"
#include "save/save_file.hpp"
#include "simulation/components.hpp"
#include "simulation/import_people.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace rct3;
    using nlohmann::json;

    std::optional<std::vector<std::byte>> read_bytes(const char* path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return std::nullopt;
        const std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const auto bytes = std::as_bytes(std::span(raw));
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    }

    json vec3(glm::vec3 v)
    {
        return json::array({ v.x, v.y, v.z });
    }

    json park_json(const simulation::SimWorld& world)
    {
        const auto& r = world.registry;
        // In ascending ids, as the park file stores them.
        std::vector<entt::entity> ids(r.view<entt::entity>().begin(), r.view<entt::entity>().end());
        std::ranges::sort(ids);
        json entities = json::array();
        for (const auto e : ids)
        {
            json j { { "id", entt::to_integral(e) } };
            if (const auto* v = r.try_get<simulation::Visitor>(e))
                j["visitor"] = { { "age", std::to_underlying(v->age) }, { "female", v->female }, { "name", v->name }, { "arrived", v->arrived } };
            if (const auto* p = r.try_get<simulation::Pose>(e))
                j["pose"] = { { "position", vec3(p->position) }, { "heading", p->heading } };
            if (const auto* n = r.try_get<simulation::Needs>(e))
                j["needs"] = { { "hunger", n->hunger }, { "thirst", n->thirst }, { "bladder", n->bladder } };
            if (const auto* m = r.try_get<simulation::Mood>(e))
                j["mood"] = { { "happiness", m->happiness }, { "energy", m->energy }, { "nausea", m->nausea } };
            if (const auto* w = r.try_get<simulation::Wallet>(e))
                j["wallet"] = { { "cash", w->cash } };
            entities.push_back(std::move(j));
        }
        return { { "tick", world.tick }, { "seed", world.seed }, { "entities", std::move(entities) } };
    }
} // namespace

int main(int argc, char** argv)
{
    const char* import = nullptr;
    const char* write = nullptr;
    const char* park = nullptr;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--import") == 0 && i + 1 < argc)
            import = argv[++i];
        else if (std::strcmp(argv[i], "--write") == 0 && i + 1 < argc)
            write = argv[++i];
        else
            park = argv[i];
    }
    if ((import == nullptr) == (park == nullptr))
    {
        std::fprintf(stderr, "usage: park-dump PARK | park-dump --import SAVE [--write PARK]\n");
        return 2;
    }

    simulation::SimWorld world;
    const char* path = import ? import : park;
    const auto bytes = read_bytes(path);
    if (!bytes)
    {
        std::fprintf(stderr, "%s: cannot read\n", path);
        return 1;
    }
    if (import)
    {
        const auto file = save::read_save(*bytes, { .verify_checksum = false });
        if (!file)
        {
            std::fprintf(stderr, "%s: %s\n", path, save::to_string(file.error()));
            return 1;
        }
        const save::ObjectIndex index(*file);
        const auto people = save::load_people(*file, index);
        if (!people)
        {
            std::fprintf(stderr, "%s: people: %s\n", path, save::to_string(people.error()));
            return 1;
        }
        simulation::import_guests(world, *people);
    }
    else
    {
        auto read = parkfile::read_park_file(*bytes);
        if (!read)
        {
            std::fprintf(stderr, "%s: %s\n", path, read.error().c_str());
            return 1;
        }
        world = std::move(*read);
    }

    std::puts(park_json(world).dump(2).c_str());
    if (write)
    {
        const auto out = parkfile::write_park_file(world);
        std::ofstream file(write, std::ios::binary);
        file.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
        if (!file)
        {
            std::fprintf(stderr, "%s: cannot write\n", write);
            return 1;
        }
    }
    return 0;
}
