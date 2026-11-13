// SPDX-License-Identifier: GPL-3.0-or-later
//
// Keeping the support columns (park/support/columns.hpp) up with the park as it changes. Whatever changes the
// park's objects, paths or ground marks the supports stale; once a frame, if they are, the objects, ground
// paths and ground are compared with how they were when last looked at, and the columns are built again on the
// tiles where any of them changed: under an item built, taken away, moved or swapped for another, beside it
// where its support points stand, and where a ground path or the ground itself changed. Each rebuilt tile loses
// its old pieces first. The support pieces are read here too, and the columns a ghost would stand on worked out.
#pragma once

#include "park/support/sets.hpp"
#include "save/model/world.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rct3::ui
{
    struct InterfaceAssets;
}

namespace rct3::scene::park
{

    struct ParkScene;

    /// How the park stood when its supports were last built.
    struct SupportWatch
    {
        struct Seen
        {
            std::string symbol;
            std::string overlay;
            std::int32_t col = 0;
            std::int32_t row = 0;
            std::int32_t rotation = 0;
            std::optional<std::int32_t> base_height;
            bool hidden = false;
            std::string support_set;
            std::vector<std::pair<int, int>> tiles; ///< Whose columns it may change
        };
        std::unordered_map<std::uint64_t, Seen> objects; ///< By id; support pieces left out
        std::vector<std::pair<int, int>> ground_paths;   ///< Tiles with a path on the ground, in order
        std::vector<std::array<float, 4>> ground;        ///< Each tile's corner heights, row by row
        bool stale = false;                              ///< Something changed since
    };

    /// Remembers how the park stands now, its supports just built.
    void watch_supports(ParkScene& scene);

    /// Builds the supports again where the park changed, when it may have.
    void refresh_supports(ParkScene& scene, const ui::InterfaceAssets& assets);

    /// The supports' pieces, read when first needed; none when the install has none.
    const rct3::park::support::SupportSets* support_sets(ParkScene& scene, const ui::InterfaceAssets& assets);

    /// What columns pass by as if it were not there: the rides' station platforms and the paving off the ground.
    [[nodiscard]] std::unordered_set<std::uint64_t> supports_passed(const ParkScene& scene);

    /// The support pieces that would hold up `ghosts`, were they added to the scene's objects.
    /// shortcut: the columns of what is there already stay as they are under a ghost; show them changed too if
    /// the difference shows.
    [[nodiscard]] std::vector<save::model::PlacedObject> supports_under(const ParkScene& scene, std::span<const save::model::PlacedObject> ghosts, const rct3::park::support::SupportSets& sets);

} // namespace rct3::scene::park
