// SPDX-License-Identifier: GPL-3.0-or-later
//
// Finishing a track for the player: the pieces that carry it on from its open end round to where its first
// piece starts, so it closes into a circuit. The way is found by trying the plainest ways first: from the open
// end, each plain piece that fits is laid in thought, and the end that leaves nearest the goal, counting the
// way come so far and the straight steps still to go across, up and along, is carried on next. Only plain
// pieces are laid (no stations, chains or pieces picked by name), and only where each meets the last.
#pragma once

#include "park/track_pieces.hpp"
#include "save/model/world.hpp"

#include <cstddef>
#include <expected>
#include <functional>
#include <optional>
#include <set>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace rct3::park
{

    /// A piece the finished track lays: which and where.
    struct LaidPlacement
    {
        LaidPiece piece;
        save::PlacedObject object; ///< As place_after sets it
    };

    enum class CompletionFailure : std::uint8_t
    {
        NoWay,      ///< Every way was tried
        OutOfTries, ///< Gave up after `tries` ends
    };

    /// A search for the pieces of `pieces` that carry a track on from `tip`, whose last piece is `last`, to
    /// `goal`: the open end a piece before its `first` piece would leave (start_of), laying only pieces `allowed`
    /// lets stand where they would. It runs a few tries at a time (search_on), so the player sees it go.
    struct CompletionSearch
    {
        std::span<const PieceShape> pieces;
        TrackTip goal;
        LaidPiece first;
        const save::TerrainGrid* grid = nullptr;
        std::function<bool(const save::PlacedObject&)> allowed;

        struct Step
        {
            TrackTip tip;
            LaidPlacement laid;
            std::size_t before = 0; ///< The step it carries on from; the first carries on from itself
            float come = 0;         ///< Tiles of track laid so far
        };
        std::vector<Step> steps;
        std::vector<std::pair<float, std::size_t>> open;                               ///< A heap of steps to carry on from, nearest the goal first
        std::set<std::tuple<long, long, long, int, int, int, std::size_t, bool>> seen; ///< Ends reached
        std::size_t tried = 0;
        std::size_t nearest = 0; ///< The step tried that came nearest the goal
    };

    [[nodiscard]] CompletionSearch start_completion(std::span<const PieceShape> pieces, const TrackTip& tip, LaidPiece last, const TrackTip& goal, LaidPiece first, const save::TerrainGrid& grid, std::function<bool(const save::PlacedObject&)> allowed);

    /// Carries `search` on for up to `tries` more ends: the way once it is found, NoWay once every way was
    /// tried, nothing while it goes on.
    [[nodiscard]] std::optional<std::expected<std::vector<LaidPlacement>, CompletionFailure>> search_on(CompletionSearch& search, std::size_t tries);

    /// The pieces laid so far toward the end that came nearest the goal.
    [[nodiscard]] std::vector<LaidPlacement> nearest_way(const CompletionSearch& search);

    /// The whole search at once, giving up with OutOfTries after `tries` ends.
    [[nodiscard]] std::expected<std::vector<LaidPlacement>, CompletionFailure> complete_track(std::span<const PieceShape> pieces, const TrackTip& tip, LaidPiece last, const TrackTip& goal, LaidPiece first, const save::TerrainGrid& grid, const std::function<bool(const save::PlacedObject&)>& allowed, std::size_t tries);

} // namespace rct3::park
