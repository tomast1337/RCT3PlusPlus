// SPDX-License-Identifier: GPL-3.0-or-later
//
// Supports under raised paths. They are not kept in a park: they are built when it loads, as placed objects
// standing on the ground under each raised path piece. Each path style has a set of support pieces, named
// by a group that the style's path pieces also carry: a foot that stands on the ground, another foot that
// reaches a little into it for ground that is not level, and middle pieces of a few heights to stack.
//
// Our stack: the foot on the ground, then middles from tallest to shortest, as many of each as still fit,
// up to the underside of the path piece. A tile whose ground already has a path, or whose piece is too near
// the ground for a foot, gets none; where two raised pieces share a tile only the lower one is held up.
#pragma once

#include "park/object_mesh.hpp"
#include "save/model/world.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rct3::park
{

    /// Item types of support pieces, as scenery items store them.
    inline constexpr std::uint32_t kSupportMiddleType = 12;
    inline constexpr std::uint32_t kSupportFootType = 14;
    inline constexpr std::uint32_t kSupportDeepFootType = 15;

    /// Where the support pieces are, relative to the game's folder and as PlacedObject::overlay names it.
    inline constexpr std::string_view kSupportOverlay = "Supports/Supports";

    struct SupportPiece
    {
        std::string symbol;
        std::int32_t levels = 0; ///< How tall it is
    };

    struct SupportSet
    {
        std::vector<SupportPiece> middles; ///< Tallest first, one per height
        std::optional<SupportPiece> foot;
        std::optional<SupportPiece> deep_foot; ///< For ground that is not level
    };

    using SupportSets = std::map<std::string, SupportSet, std::less<>>;

    /// Adds a piece to the set of `group`, by its item type; other types are ignored, and so is a second
    /// piece of a kind and height the set already has.
    void add_support_piece(SupportSets& sets, std::string_view group, std::uint32_t type, std::string_view symbol, std::int32_t levels);

    /// The support sets of the overlay at `path`.
    [[nodiscard]] std::expected<SupportSets, std::string> load_support_sets(const std::filesystem::path& path);

    /// One piece of a stack and the level its base stands at.
    struct StackedPiece
    {
        std::string symbol;
        std::int32_t level = 0;
    };

    /// The pieces holding up something whose underside is at level `top`, over ground whose tile corners
    /// go from `ground_low` to `ground_high`. Empty when the gap is too small for a foot.
    [[nodiscard]] std::vector<StackedPiece> support_stack(const SupportSet& set, float ground_low, float ground_high, std::int32_t top);

    /// Placed supports under the raised paths among `paths`, whose pieces are among `objects` with their
    /// models in `models`. New objects take ids after the largest in `objects`.
    [[nodiscard]] std::vector<save::PlacedObject> path_supports(std::span<const save::PathTile> paths, std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid, const SupportSets& sets);

} // namespace rct3::park
