// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park's animals, drawn. One out of its crate is drawn with its species' own model, posed every frame in
// the motion that fits what it is doing: walking, eating or drinking at the troughs, asleep, or standing. One
// still in its crate, or of a species whose model cannot be read, is drawn as the crate. The park's zoo animals
// as saved are drawn the same way, each where the save placed it, standing.
#pragma once

#include "config/game_install.hpp"
#include "park/animal_mesh.hpp"
#include "park/moving_meshes.hpp"
#include "render/device.hpp"
#include "save/model/world.hpp"
#include "simulation/entt.hpp"
#include "ui/ui_textures.hpp"

#include <optional>
#include <vector>

namespace rct3::scene
{

    struct ParkScene;

    struct DrawnAnimals
    {
        std::optional<park::AnimalLibrary> library;
        std::vector<park::PlacedAnimal> saved_placed; ///< The saved zoo animals; bodies point into `library`
        std::vector<float> offsets;                   ///< By saved animal, seconds its motion runs ahead, so they do not move as one
        std::vector<std::size_t> saved;               ///< By saved animal, its place in the saved zoo animals
        std::vector<park::PlacedAnimal> placed;       ///< All drawn with their models, as last posed
        park::AnimalRuns runs { };
        park::ObjectMesh mesh; ///< As last posed
        ui::UiTextures textures;
        park::MovingMeshes crates;            ///< The animals still in their crates
        std::vector<entt::entity> crated { }; ///< By crate, its animal
    };

    /// Reads the models of the saved zoo animals in `scene.people` and uploads their pictures.
    void load_zoo_animals(render::Device& device, ParkScene& scene, const config::GameInstall& install);

    /// The crate animals come in, as an object to load the model of.
    [[nodiscard]] save::PlacedObject animal_crate();

    /// Reads the models of animals that came since and uploads their pictures, and the crate's; the crate's
    /// model must be loaded.
    void animals_changed(render::Device& device, ParkScene& scene);

    /// Poses the animals and their crates as they are now, at the park's clock.
    void place_animals(ParkScene& scene);

} // namespace rct3::scene
