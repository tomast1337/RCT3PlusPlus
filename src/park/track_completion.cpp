// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track_completion.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <tuple>
#include <utility>

namespace rct3::park
{

    namespace
    {
        /// How near an end must come to the goal to meet it, in park units.
        constexpr float kMeet = 0.1f;

        float to_go(const CompletionSearch& s, const glm::vec3& at)
        {
            return (std::abs(s.goal.position.x - at.x) + std::abs(s.goal.position.y - at.y) + std::abs(s.goal.position.z - at.z)) / s.grid->tile_size_x;
        }

        auto key_of(const TrackTip& at, LaidPiece piece)
        {
            return std::tuple { std::lround(at.position.x * 4), std::lround(at.position.y * 4), std::lround(at.position.z * 4), static_cast<int>(at.end.slope), static_cast<int>(at.end.bank), static_cast<int>(at.end.heading), piece.index, piece.reversed };
        }

        std::vector<LaidPlacement> way_to(const CompletionSearch& search, std::size_t step)
        {
            std::vector<LaidPlacement> way;
            for (std::size_t s = step; s != 0; s = search.steps[s].before)
                way.push_back(search.steps[s].laid);
            std::ranges::reverse(way);
            return way;
        }
    } // namespace

    CompletionSearch start_completion(std::span<const PieceShape> pieces, const TrackTip& tip, LaidPiece last, const TrackTip& goal, LaidPiece first, const save::TerrainGrid& grid, std::function<bool(const save::PlacedObject&)> allowed)
    {
        CompletionSearch s { .pieces = pieces, .goal = goal, .first = first, .grid = &grid, .allowed = std::move(allowed) };
        if (first.index >= pieces.size() || last.index >= pieces.size() || grid.tile_size_x <= 0 || grid.tile_size_z <= 0)
            return s;
        s.steps.push_back({ tip, { last, { } }, 0, 0 });
        s.open.push_back({ to_go(s, tip.position), 0 });
        s.seen.insert(key_of(tip, last));
        return s;
    }

    std::optional<std::expected<std::vector<LaidPlacement>, CompletionFailure>> search_on(CompletionSearch& search, std::size_t tries)
    {
        const auto meets_goal = [&](const TrackTip& at, LaidPiece piece) {
            const PieceShape& a = search.pieces[piece.index];
            const PieceShape& b = search.pieces[search.first.index];
            return glm::length(at.position - search.goal.position) < kMeet && at.end == search.goal.end && a.record.side == b.record.side && ends_meet(laid_end_join(a, piece.reversed), laid_start_join(b, search.first.reversed));
        };
        for (std::size_t i = 0; i < tries; ++i, ++search.tried)
        {
            if (search.open.empty())
                return std::unexpected(CompletionFailure::NoWay);
            std::ranges::pop_heap(search.open, std::greater<> { });
            const std::size_t at = search.open.back().second;
            search.open.pop_back();
            if (to_go(search, search.steps[at].tip.position) < to_go(search, search.steps[search.nearest].tip.position))
                search.nearest = at;
            for (const LaidPiece next : pieces_after(search.pieces, search.steps[at].tip.end, search.steps[at].laid.piece))
            {
                const PieceShape& shape = search.pieces[next.index];
                if (shape.use != PieceUse::Plain || (shape.toggles & toggle_bit(PieceToggle::Chain)) != 0)
                    continue;
                auto placed = place_after(search.steps[at].tip, shape, next.reversed, *search.grid);
                if (!placed || !search.allowed(placed->object) || !search.seen.insert(key_of(placed->next, next)).second)
                    continue;
                const float come = search.steps[at].come + std::max(glm::length(shape.line.end - shape.line.start) / search.grid->tile_size_x, 1.0f);
                search.steps.push_back({ placed->next, { next, std::move(placed->object) }, at, come });
                if (meets_goal(search.steps.back().tip, next))
                    return way_to(search, search.steps.size() - 1);
                search.open.push_back({ come + to_go(search, search.steps.back().tip.position), search.steps.size() - 1 });
                std::ranges::push_heap(search.open, std::greater<> { });
            }
        }
        return std::nullopt;
    }

    std::vector<LaidPlacement> nearest_way(const CompletionSearch& search)
    {
        return search.steps.empty() ? std::vector<LaidPlacement> { } : way_to(search, search.nearest);
    }

    std::expected<std::vector<LaidPlacement>, CompletionFailure> complete_track(std::span<const PieceShape> pieces, const TrackTip& tip, LaidPiece last, const TrackTip& goal, LaidPiece first, const save::TerrainGrid& grid, const std::function<bool(const save::PlacedObject&)>& allowed, std::size_t tries)
    {
        CompletionSearch search = start_completion(pieces, tip, last, goal, first, grid, allowed);
        return search_on(search, tries).value_or(std::unexpected(CompletionFailure::OutOfTries));
    }

} // namespace rct3::park
