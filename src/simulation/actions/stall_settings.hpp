// SPDX-License-Identifier: GPL-3.0-or-later
//
// Actions on a stall's menu as its window changes it: what it sells, at what prices, under what names and colours,
// with which extras. A stall synchronised passes each change on to every stall of its kind.
#pragma once

#include "save/model/rides.hpp"
#include "simulation/actions/world.hpp"

#include <string>
#include <vector>

namespace rct3::simulation::actions
{

    enum class StallEdit : u8
    {
        Sync,       ///< `value` nought or one
        SellAs,     ///< A custom stall sells as kind `text` (with its overlay `overlay`), its menu `items` afresh
        ItemPrice,  ///< `item`'s price to `value`, in hundredths
        RemoveItem, ///< `item` off the menu
        AddItem,    ///< `items`' one item onto the menu at place `value`
        MoveItem,   ///< `item` to place `value`
        ItemExtras, ///< `item`'s extras to the bits of `value`
        ItemName,   ///< `item` named `text`
        ItemColour, ///< `item`'s colour in slot `pot` to `value`
        ExtraLevel, ///< Extra `item`'s level to `value`
    };

    /// Changes a stall's menu.
    struct ChangeStall
    {
        save::model::ObjectId stall = 0;
        StallEdit edit = StallEdit::Sync;
        usize item = 0;
        i32 value = 0;
        usize pot = 0;
        std::string text { };
        std::string overlay { };
        std::vector<save::model::StallItem> items { };
    };

    [[nodiscard]] Outcome check(const World& world, const ChangeStall& action);
    void apply(World& world, const ChangeStall& action);

} // namespace rct3::simulation::actions
