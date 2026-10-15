// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the info panel shows about a prop placed in the park: a tab that pulls it down at once, and tabs for
// what some props do, such as a litter bin's state or a sign's words. A plain prop has no page open; the
// strip then only names it.
#pragma once

#include "save/model/world.hpp"
#include "ui/screens/info_panel.hpp"
#include "ui/string_table.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::ui
{

    enum class SceneryTab : std::uint8_t
    {
        Destroy, ///< Pulls the prop down; has no page
        Bin,     ///< How full a litter bin is and when it was emptied
        Sign,    ///< What a sign says
    };

    struct SceneryTabInfo
    {
        SceneryTab tab = SceneryTab::Destroy;
        int slot = 0;          ///< Its place on the ring
        std::string_view icon; ///< Skin
        std::string_view help; ///< String key
        bool action = false;   ///< Acts at once instead of opening a page
    };

    /// The prop a page is written about: the asset it is made from and what it does, when it is a bin or a sign.
    struct SceneryFacts
    {
        std::string_view symbol;
        const save::BinState* bin = nullptr;
        const save::SignState* sign = nullptr;
    };

    /// The tabs of the prop, in the order of their slots.
    [[nodiscard]] std::vector<SceneryTabInfo> scenery_tabs(const SceneryFacts& facts);

    /// The prop's name, from the string named after its asset; empty when there is none.
    [[nodiscard]] std::u16string scenery_name(const SceneryFacts& facts, const StringTable& strings);

    /// The page of `tab`, or the page with no tab open when there is none.
    [[nodiscard]] InfoPage scenery_page(std::optional<SceneryTab> tab, const SceneryFacts& facts, const StringTable& strings);

} // namespace rct3::ui
