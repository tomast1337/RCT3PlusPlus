// SPDX-License-Identifier: GPL-3.0-or-later
//
// Placing many of one scenery item at once. A drag from one tile to another places a run of it: in a line along
// the tile edges for walls and fences, on every tile of the area spanned for other items, each in the quarter and
// turn of the first. A scatter brush instead sprinkles items round the pointer, at random spots, kept apart.
#pragma once

#include "core/types.hpp"
#include "math/glm.hpp"
#include "park/object/placement.hpp"
#include "random/generator.hpp"
#include "save/model/world.hpp"

#include <array>
#include <optional>
#include <span>
#include <vector>

namespace rct3::park
{

    /// Copies of `first`, the item where a drag began, from its tile towards `last`, one a `span` (the tiles it covers
    /// along the columns and rows, as turned) apart: in a line along the edge it stands on for an edge item, over the
    /// whole area for others; `first` itself comes first.
    [[nodiscard]] std::vector<save::model::PlacedObject> dragged_items(const save::model::PlacedObject& first, save::model::Tile last, object::Positioning positioning, std::array<i32, 2> span = { 1, 1 });

    /// How far apart a scatter brush keeps its items, in tiles.
    enum class Scatter : u8
    {
        Off,
        Dense,
        Sparse,
    };

    /// The next spot, in tiles from the grid's corner, a scatter brush round `centre` puts an item on: a random point
    /// within `radius` of it, at least the spacing of `scatter` from each of `taken`. None when a few tries find no
    /// such room, or the brush is off.
    [[nodiscard]] std::optional<glm::vec2> scatter_spot(random::RandomState& stream, glm::vec2 centre, f32 radius, Scatter scatter, std::span<const glm::vec2> taken);

} // namespace rct3::park
