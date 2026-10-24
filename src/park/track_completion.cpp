// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track_completion.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <set>
#include <tuple>
#include <utility>

namespace rct3::park
{

    namespace
    {
        /// How near an end must come to the goal to meet it, in park units.
        constexpr float kMeet = 0.1f;

        struct Step
        {
            TrackTip tip;
            LaidPlacement laid;
            std::size_t before = 0; ///< The step it carries on from; the first carries on from itself
            float come = 0;         ///< Tiles of track laid so far
        };
    } // namespace

    std::expected<std::vector<LaidPlacement>, CompletionFailure> complete_track(std::span<const PieceShape> pieces, const TrackTip& tip, LaidPiece last, const TrackTip& goal, LaidPiece first, const save::TerrainGrid& grid, const std::function<bool(const save::PlacedObject&)>& allowed, std::size_t tries)
    {
        if (first.index >= pieces.size() || last.index >= pieces.size() || grid.tile_size_x <= 0 || grid.tile_size_z <= 0)
            return std::unexpected(CompletionFailure::NoWay);
        const float tile = grid.tile_size_x;
        const auto to_go = [&](const glm::vec3& at) { return (std::abs(goal.position.x - at.x) + std::abs(goal.position.y - at.y) + std::abs(goal.position.z - at.z)) / tile; };
        const auto meets_goal = [&](const TrackTip& at, LaidPiece piece) {
            const PieceShape& a = pieces[piece.index];
            const PieceShape& b = pieces[first.index];
            return glm::length(at.position - goal.position) < kMeet && at.end == goal.end && a.record.side == b.record.side && ends_meet(laid_end_join(a, piece.reversed), laid_start_join(b, first.reversed));
        };
        // Ends already reached, by where they are, which way they leave and the piece they leave from.
        const auto key = [](const TrackTip& at, LaidPiece piece) { return std::tuple { std::lround(at.position.x * 4), std::lround(at.position.y * 4), std::lround(at.position.z * 4), at.end.slope, at.end.bank, at.end.heading, piece.index, piece.reversed }; };
        std::set<decltype(key(tip, last))> seen;
        std::vector<Step> steps { { tip, { last, { } }, 0, 0 } };
        using Open = std::pair<float, std::size_t>;
        std::priority_queue<Open, std::vector<Open>, std::greater<>> open;
        open.push({ to_go(tip.position), 0 });
        seen.insert(key(tip, last));
        for (std::size_t tried = 0; !open.empty(); ++tried)
        {
            if (tried == tries)
                return std::unexpected(CompletionFailure::OutOfTries);
            const std::size_t at = open.top().second;
            open.pop();
            for (const LaidPiece next : pieces_after(pieces, steps[at].tip.end, steps[at].laid.piece))
            {
                const PieceShape& shape = pieces[next.index];
                if (shape.use != PieceUse::Plain || (shape.toggles & toggle_bit(PieceToggle::Chain)) != 0)
                    continue;
                auto placed = place_after(steps[at].tip, shape, next.reversed, grid);
                if (!placed || !allowed(placed->object) || !seen.insert(key(placed->next, next)).second)
                    continue;
                const float come = steps[at].come + std::max(glm::length(shape.line.end - shape.line.start) / tile, 1.0f);
                steps.push_back({ placed->next, { next, std::move(placed->object) }, at, come });
                if (meets_goal(steps.back().tip, next))
                {
                    std::vector<LaidPlacement> way;
                    for (std::size_t s = steps.size() - 1; s != 0; s = steps[s].before)
                        way.push_back(steps[s].laid);
                    std::ranges::reverse(way);
                    return way;
                }
                open.push({ come + to_go(steps.back().tip.position), steps.size() - 1 });
            }
        }
        return std::unexpected(CompletionFailure::NoWay);
    }

} // namespace rct3::park
