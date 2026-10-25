// SPDX-License-Identifier: GPL-3.0-or-later
//
// Keeping the support columns (park/support_columns.hpp) up with the park as it changes. Whatever changes the
// park's objects, paths or ground marks the supports stale; once a frame, if they are, the objects, ground
// paths and ground are compared with how they were when last looked at, and the columns are built again on the
// tiles where any of them changed: under an item built, taken away, moved or swapped for another, beside it
// where its support points stand, and where a ground path or the ground itself changed. Each rebuilt tile loses
// its old pieces first.
#pragma once

#include "save/model/world.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rct3::ui
{
    struct InterfaceAssets;
}

namespace rct3::scene
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

} // namespace rct3::scene
