// SPDX-License-Identifier: GPL-3.0-or-later
//
// Research categories: the groups that scenery items and rides are researched in. A category may
// require other categories to be complete first.
#pragma once

#include "ovl/overlay.hpp"
#include "ovl/scenery_item.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    /// Structure of 40 bytes. Layout verified on every category in the install, meanings partly unknown.
    struct ResearchCategory
    {
        /// 0, 1 or 2. Unconfirmed: 2 appears on categories whose names end in a marker for items unlocked
        /// by playing a scenario, 1 on themed groups, 0 on the rest.
        std::uint32_t kind = 0;
        std::string_view name;      ///< The category's own name as stored in the structure
        std::string_view name_text; ///< "txt" item
        /// Level the category starts at: 0, 10, 20, 25 or 50 seen. Park files of the base game hold the same
        /// value as the category's current level.
        float start_level = 0;
        /// Cost of developing the category: 10, or 1 for the unlockable kind. Park files hold the same value.
        float develop_cost = 0;
        /// Categories that must reach a level first, as 12-byte rows. A row whose reference is missing gives an
        /// empty name.
        std::vector<SceneryItemResearch> prerequisites;
    };

    struct ResearchCategoryEntry
    {
        std::string_view name; ///< Symbol name without the tag
        std::optional<ResearchCategory> category;
    };

    /// Research categories of one part of an overlay: pointer symbols tagged "rcg". Views point into the
    /// overlay's bytes.
    [[nodiscard]] std::vector<ResearchCategoryEntry> read_research_categories(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
