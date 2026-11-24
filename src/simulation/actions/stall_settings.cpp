// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/actions/stall_settings.hpp"

#include "park/ride/placement.hpp"
#include "simulation/actions/attraction_state.hpp"
#include "simulation/actions/ids.hpp"
#include "simulation/components.hpp"
#include "simulation/import_park.hpp"

#include <algorithm>
#include <utility>

namespace rct3::simulation::actions
{

    Outcome check(const World& world, const ChangeStall& action)
    {
        const std::optional<save::model::Stall&> stall = stall_of(world.rides, action.stall);
        if (!stall)
            return std::unexpected(Refusal::Missing);
        switch (action.edit)
        {
            using enum StallEdit;
            case Sync:
                break;
            case SellAs:
                if (action.text.empty())
                    return std::unexpected(Refusal::Missing);
                break;
            case AddItem:
                if (action.items.size() != 1)
                    return std::unexpected(Refusal::Missing);
                break;
            case ExtraLevel:
                if (action.item >= stall->extras.size())
                    return std::unexpected(Refusal::Missing);
                break;
            case ItemColour:
                if (action.pot >= save::model::RideColours { }.size())
                    return std::unexpected(Refusal::Missing);
                [[fallthrough]];
            default:
                if (action.item >= stall->items.size())
                    return std::unexpected(Refusal::Missing);
        }
        return Checked { };
    }

    void apply(World& world, const ChangeStall& action)
    {
        const std::optional<save::model::Stall&> found = stall_of(world.rides, action.stall);
        save::model::Stall& stall = *found;
        switch (action.edit)
        {
            using enum StallEdit;
            case Sync:
                stall.sync = action.value != 0;
                break;
            case SellAs:
                // A custom stall picks up the kind's goods at its prices, and the extras they take afresh.
                stall.sells_as = action.text;
                stall.sells_as_overlay = action.overlay;
                stall.items = action.items;
                stall.extras.clear();
                break;
            case ItemPrice:
                stall.items[action.item].price = { action.value };
                break;
            case RemoveItem:
                stall.items.erase(stall.items.begin() + static_cast<std::ptrdiff_t>(action.item));
                break;
            case AddItem:
                stall.items.insert(stall.items.begin() + std::clamp<std::ptrdiff_t>(action.value, 0, std::ssize(stall.items)), action.items.front());
                break;
            case MoveItem:
                rct3::park::ride::move_item(stall, action.item, static_cast<usize>(std::max(action.value, 0)));
                break;
            case ItemExtras:
                stall.items[action.item].extras = static_cast<u32>(action.value);
                break;
            case ItemName:
                stall.items[action.item].name = action.text;
                break;
            case ItemColour:
                stall.items[action.item].colours[action.pot] = static_cast<u8>(action.value);
                break;
            case ExtraLevel:
                stall.extras[action.item].level = action.value;
                break;
        }
        const usize index = static_cast<usize>(&stall - world.rides.stalls.data());
        if (stall.sync)
            rct3::park::ride::copy_menu(world.rides.stalls, index);
        for (usize i = 0; i < world.rides.stalls.size(); ++i)
        {
            save::model::Stall& s = world.rides.stalls[i];
            if (i != index && !(stall.sync && rct3::park::ride::same_kind(s, stall)))
                continue;
            // shortcut: a stall left with nothing to sell closes; the game's way with it is not known,
            // upgrade once it is.
            if (s.items.empty() && s.base.state == save::model::AttractionState::Open)
                put_in_state(world.sim, s.base, save::model::AttractionState::Closed);
            // An item put on brings the extras it takes.
            rct3::park::ride::offer_extras(s, world.carried.items);
            restock(world.sim, s, world.carried);
            // One that had nothing to sell was never joined to the paths; it is once it has.
            bool joined = false;
            for (const auto [e, a] : world.sim.registry.view<Attraction>().each())
                joined = joined || a.saved == s.base.id;
            if (!joined && !s.items.empty() && !std::ranges::contains(world.unjoined_stalls, save::model::StallIndex { i }))
                world.unjoined_stalls.push_back(save::model::StallIndex { i });
        }
    }

} // namespace rct3::simulation::actions
