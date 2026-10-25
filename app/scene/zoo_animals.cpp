// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/zoo_animals.hpp"

#include "logging/log.hpp"
#include "profiling/profiler.hpp"
#include "scene/park_scene.hpp"
#include "scene/park_setup.hpp"

#include <array>
#include <string_view>

namespace rct3::scene
{

    void load_zoo_animals(render::Device& device, ParkScene& scene, const config::GameInstall& install)
    {
        if (scene.people.zoo_animals.empty())
            return;
        const profiling::Scope zone("ZooAnimals");
        constexpr std::string_view kAnimalFolder = "WildAnimals";
        const auto folder = install.find(kAnimalFolder);
        if (!folder)
            return;
        const std::string name = folder->filename().string();
        const std::array<std::string_view, 1> folders { name };
        DrawnAnimals& drawn = scene.drawn_animals;
        drawn.library.emplace(install.root(), folders);
        for (std::size_t i = 0; i < scene.people.zoo_animals.size(); ++i)
        {
            const save::ZooAnimal& animal = scene.people.zoo_animals[i];
            if (animal.in_crate)
                continue; // Drawn as its crate
            const auto body = drawn.library->body(animal.species, animal.is_adult, animal.is_male);
            if (!body)
                continue;
            const ovl::AnimalMotion motion = park::motion_seconds(*body, ovl::AnimalMotion::Stand) > 0 ? ovl::AnimalMotion::Stand : ovl::AnimalMotion::Idle1;
            park::PlacedAnimal placed { .body = &*body, .motion = motion };
            // Stored a row per axis, which is a column each here.
            for (glm::length_t c = 0; c < 4; ++c)
            {
                for (glm::length_t r = 0; r < 4; ++r)
                    placed.world[c][r] = animal.world[static_cast<std::size_t>(c * 4 + r)];
            }
            drawn.offsets.push_back(static_cast<float>(drawn.placed.size()) * 0.37f);
            drawn.placed.push_back(placed);
            drawn.saved.push_back(i);
        }
        drawn.library->close_files();
        park::GroundImages shrunk; // Copied only when they are shrunk; the library keeps its own
        if (scene.texture_detail < 1)
        {
            shrunk = drawn.library->pictures();
            fit_texture_detail(scene, shrunk);
        }
        if (auto uploaded = ui::upload_textures(device, scene.texture_detail < 1 ? shrunk : drawn.library->pictures()))
            drawn.textures = std::move(*uploaded);
        else
            logging::logger("park").warning("zoo animal pictures: {}", uploaded.error());
        logging::logger("park").info("{} of {} zoo animals shown, {} pictures", drawn.placed.size(), scene.people.zoo_animals.size(), drawn.library->pictures().size());
    }

    void refresh_zoo_animals(ParkScene& scene)
    {
        DrawnAnimals& drawn = scene.drawn_animals;
        if (drawn.placed.empty())
            return;
        // shortcut: every animal is posed on the frame's thread at its finest level, every frame; fine for the
        // few dozen a zoo holds, move to a worker and pick levels by distance as people do once parks hold more.
        for (std::size_t i = 0; i < drawn.placed.size(); ++i)
            drawn.placed[i].time = scene.park_clock + drawn.offsets[i];
        drawn.mesh = park::animal_mesh(drawn.placed, drawn.runs);
    }

} // namespace rct3::scene
