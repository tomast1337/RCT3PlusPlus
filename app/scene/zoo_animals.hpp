// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park's zoo animals as saved, drawn with their own models: each where the save placed it, standing in
// its stand motion (or its first idle where it has none), posed afresh every frame.
#pragma once

#include "config/game_install.hpp"
#include "park/animal_mesh.hpp"
#include "render/device.hpp"
#include "ui/ui_textures.hpp"

#include <optional>
#include <vector>

namespace rct3::scene
{

    struct ParkScene;

    struct DrawnAnimals
    {
        std::optional<park::AnimalLibrary> library;
        std::vector<park::PlacedAnimal> placed; ///< Bodies point into `library`
        std::vector<float> offsets;             ///< By animal, seconds its motion runs ahead, so they do not move as one
        std::vector<std::size_t> saved;         ///< By animal, its place in the saved zoo animals
        park::AnimalRuns runs { };
        park::ObjectMesh mesh; ///< As last posed
        ui::UiTextures textures;
    };

    /// Reads the models of the saved zoo animals in `scene.people` and uploads their pictures.
    void load_zoo_animals(render::Device& device, ParkScene& scene, const config::GameInstall& install);

    /// Poses the zoo animals at the park's clock.
    void refresh_zoo_animals(ParkScene& scene);

} // namespace rct3::scene
