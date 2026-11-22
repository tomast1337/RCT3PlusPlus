// SPDX-License-Identifier: GPL-3.0-or-later
//
// Zoo animals posed and placed, as one mesh grouped by picture. Each animal's bones are posed by its
// motion at its time, and every vertex follows the bones it is weighted to.
#pragma once

#include "core/types.hpp"
#include "park/animal_models.hpp"
#include "park/object/mesh.hpp"
#include "park/object/models.hpp"
#include "units/units.hpp"

#include <array>
#include <cstddef>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rct3::park
{

    /// One animal to draw.
    struct PlacedAnimal
    {
        const AnimalBody* body = nullptr;
        usize level = 0; ///< Of detail, 0 the finest; past the coarsest drawn level, that one
        ovl::animal::AnimalMotion motion = ovl::animal::AnimalMotion::Stand;
        units::Seconds time { };  ///< Into the motion
        glm::mat4 world { 1.0f }; ///< Model to park
    };

    /// The triangles of animals gathered for one picture.
    struct AnimalRun
    {
        std::vector<GroundVertex> vertices { };
        std::vector<u32> indices { };
        object::PartBlend blend = object::PartBlend::Opaque;
        bool one_sided = true;
    };
    /// Those of every picture, by picture. Kept by a caller that builds mesh after mesh, so the lists' memory
    /// is reused.
    using AnimalRuns = std::map<std::string, AnimalRun, std::less<>>;

    /// How far away animals are drawn at each level of detail, finest first; beyond the last they are not.
    inline constexpr std::array kAnimalDistances { 60.0f, 160.0f, 500.0f }; ///< Chosen by eye: a little nearer than people, as the zoo's are few and big

    /// The level of detail an animal `distance` away is drawn at, with the animals' detail and draw distance options
    /// `scale` (detail for the finer levels, draw distance for the last), as objects' levels are scaled. Nothing
    /// when it is too far to draw.
    [[nodiscard]] std::optional<usize> animal_level(f32 distance, object::LevelScale scale);

    /// The mesh of `animals`, gathered in `runs`, which is emptied first. Batches come opaque first, then
    /// cut-out, then see-through.
    [[nodiscard]] object::ObjectMesh animal_mesh(std::span<const PlacedAnimal> animals, AnimalRuns& runs);

} // namespace rct3::park
