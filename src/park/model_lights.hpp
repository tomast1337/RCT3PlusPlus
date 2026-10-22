// SPDX-License-Identifier: GPL-3.0-or-later
//
// Lamps that models carry: lights that shine on the park round them at night. A model marks each light with
// named points (attachment points of a static shape, or bones of a bone shape). A point whose name holds
// "lightstart" and a number places light number N; the rest of the light comes from words after it in the
// same name, separated by underscores, or from other points with the same number:
//
//  - Colour: a word `r<red>g<green>b<blue>` (each 0 to 255), or `flexi<slot>` for the colour the player gives
//    the item's slot (0 to 2). Else a point "lightcolour" N named the same way. Else white.
//  - Reach: how far from the start a point "lightend" N lies; else a word `radius<number>`, in metres, or in
//    centimetres with "cm" after the number. A light with neither is left out.
//
// Words before "lightstart" (seen: "simple", "medium", "nb") and a word naming the kind of lamp ("lamp",
// "torch") are read past: every light shines alike here.
#pragma once

#include "math/glm.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace rct3::park
{

    /// A named point of a model, model space.
    struct NamedPlace
    {
        std::string_view name;
        glm::mat4 place { 1.0f };
        std::int32_t bone = -1; ///< The bone it is, for a bone shape
    };

    struct ModelLight
    {
        glm::vec3 position { 0.0f };    ///< Model space
        glm::vec3 colour { 1.0f };      ///< 0 to 1 a channel
        std::optional<int> colour_slot; ///< When set, the item's colour in this slot instead of `colour`
        float radius = 0;               ///< How far it reaches, world units
        std::int32_t bone = -1;         ///< The bone that carries it, for a bone shape
    };

    /// The lights `places` mark, in light number order.
    [[nodiscard]] std::vector<ModelLight> model_lights(std::span<const NamedPlace> places);

} // namespace rct3::park
