// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/scenery_runs.hpp"

#include "core/types.hpp"
#include "park/turns.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rct3::park
{

    std::vector<save::model::PlacedObject> dragged_items(const save::model::PlacedObject& first, save::model::Tile last, object::Positioning positioning, std::array<i32, 2> span)
    {
        save::model::Tile to = last;
        // An edge item runs along its edge: one on a column's side keeps to the column, one on a row's to the row.
        if (positioning == object::Positioning::Edge)
        {
            const Side side = object::faced_side(ModelTurn::from(first.rotation));
            if (side == Side::kColBefore || side == Side::kNextCol)
                to.col = first.col;
            else
                to.row = first.row;
        }
        const i32 col_step = (to.col >= first.col ? 1 : -1) * std::max(1, span[0]);
        const i32 row_step = (to.row >= first.row ? 1 : -1) * std::max(1, span[1]);
        std::vector<save::model::PlacedObject> out;
        for (i32 col = first.col; (to.col - col) * col_step >= 0; col += col_step)
            for (i32 row = first.row; (to.row - row) * row_step >= 0; row += row_step)
            {
                save::model::PlacedObject o = first;
                o.col = col;
                o.row = row;
                out.push_back(std::move(o));
            }
        return out;
    }

    std::optional<glm::vec2> scatter_spot(random::RandomState& stream, glm::vec2 centre, f32 radius, Scatter scatter, std::span<const glm::vec2> taken)
    {
        if (scatter == Scatter::Off)
            return std::nullopt;
        constexpr i32 kTries = 8; // Chosen by eye: enough to find the gaps left, few enough each frame
        const f32 spacing = scatter == Scatter::Dense ? 1.0f : 2.0f;
        for (i32 i = 0; i < kTries; ++i)
        {
            // Uniform over the disc: the square root keeps the middle from crowding.
            const auto along = static_cast<f32>(random::between(stream, 0.0, 2 * std::numbers::pi));
            const auto out = radius * std::sqrt(static_cast<f32>(random::unit(stream)));
            const glm::vec2 spot = centre + out * glm::vec2 { std::cos(along), std::sin(along) };
            if (std::ranges::none_of(taken, [&](glm::vec2 t) { return glm::distance(t, spot) < spacing; }))
                return spot;
        }
        return std::nullopt;
    }

} // namespace rct3::park
