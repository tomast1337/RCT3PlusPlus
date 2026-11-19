// SPDX-License-Identifier: GPL-3.0-or-later
//
// Zoo animals posed and placed, as one mesh grouped by picture. Each animal's bones are posed by its
// motion at its time, and every vertex follows the bones it is weighted to.
#pragma once

#include "core/types.hpp"
#include "park/animal_models.hpp"
#include "park/object/mesh.hpp"
#include "units/units.hpp"

#include <cstddef>
#include <map>
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

    /// The mesh of `animals`, gathered in `runs`, which is emptied first. Batches come opaque first, then
    /// cut-out, then see-through.
    [[nodiscard]] object::ObjectMesh animal_mesh(std::span<const PlacedAnimal> animals, AnimalRuns& runs);

} // namespace rct3::park
