// SPDX-License-Identifier: GPL-3.0-or-later
//
// The yes-or-no boxes the park asks in: whether to leave the park, to pull attractions down, or to sell a pool
// complex, to remove a lake that has waterfalls, the waterfalls a rising lake would flood, or to set an animal free. Each box is asked once at a time, by its name; the answer comes back in the park's update.
#pragma once

#include "core/types.hpp"
#include "park/attraction_objects.hpp"
#include "save/model/world.hpp"
#include "simulation/entt.hpp"
#include "ui/palette.hpp"
#include "ui/string_table.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

namespace rct3::scene::park
{

    struct ParkScene;

    inline constexpr std::string_view kQuestionName = "leave_tutorial";
    inline constexpr std::string_view kDemolishName = "demolish_question";
    inline constexpr std::string_view kSellPoolName = "sell_pool_question";
    inline constexpr std::string_view kLakeName = "lake_question";
    inline constexpr std::string_view kFloodName = "flood_question";
    inline constexpr std::string_view kReleaseName = "release_question";

    /// Asks whether to leave the park.
    void ask_leave(ParkScene& scene, const ui::StringTable& strings, const ui::Palette& palette);

    /// Asks whether to pull `refs` down, naming them; the answer comes back in the park's update.
    void ask_demolish(ParkScene& scene, const ui::StringTable& strings, const ui::Palette& palette, std::vector<rct3::park::AttractionRef> refs);

    /// Asks whether to sell the pool complex the changing room `room` opens on.
    void ask_sell_pool_complex(ParkScene& scene, const ui::StringTable& strings, const ui::Palette& palette, usize room);

    /// Asks whether to remove the lake holding `tile`, which has waterfalls that go with it.
    void ask_remove_lake(ParkScene& scene, const ui::StringTable& strings, const ui::Palette& palette, save::model::Tile tile);

    /// Asks whether the waterfalls pouring from `tops` should go, which water about to be poured would flood.
    void ask_flood_falls(ParkScene& scene, const ui::StringTable& strings, const ui::Palette& palette, std::vector<save::model::WorldPoint3> tops);

    /// Asks whether to set `animal` free in the wild; one that may not go (simulation::may_release) is refused
    /// instead, the player told why.
    void ask_release(ParkScene& scene, const ui::StringTable& strings, const ui::Palette& palette, entt::entity animal);

    /// Sets `animal` free when it may go (simulation::release_animal), closing its window and telling the player
    /// of the publicity; one that may not go is refused as ask_release refuses it. Returns whether it went.
    bool release_animal(ParkScene& scene, entt::entity animal);

} // namespace rct3::scene::park
