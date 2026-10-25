// SPDX-License-Identifier: GPL-3.0-or-later
//
// The things stalls sell and guests carry, as a stall's menu shows them: each one's name, picture and the
// extras it can take, read from the carried items' overlays.
#pragma once

#include "ovl/overlay.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace rct3::park
{

    /// A carried item or an extra.
    struct CarriedKind
    {
        std::string symbol;              ///< With its tag, as `Burger:cid` or `Ketchup:ced`
        std::string name_text;           ///< Key of its name among the game's texts
        std::string icon;                ///< Skin
        std::vector<std::string> extras; ///< Of an item, with their tags, in its own order
        bool paintable = false;          ///< Of an item, whether it takes the player's colours
    };

    struct CarriedKinds
    {
        std::vector<CarriedKind> items;
        std::vector<CarriedKind> extras;
    };

    /// Adds what `overlay` holds to `kinds`.
    void add_carried_kinds(const ovl::Overlay& overlay, CarriedKinds& kinds);

    /// Every overlay of carried items in `folder`, the common files only.
    [[nodiscard]] std::vector<std::filesystem::path> carried_overlays(const std::filesystem::path& folder);

} // namespace rct3::park
