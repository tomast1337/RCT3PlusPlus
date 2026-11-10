// SPDX-License-Identifier: GPL-3.0-or-later
//
// The yes-or-no boxes the park asks in: whether to leave the park, to pull attractions down, or to sell a pool
// complex. Each box is asked once at a time, by its name; the answer comes back in the park's update.
#pragma once

#include "park/attraction_objects.hpp"
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

    /// Asks whether to leave the park.
    void ask_leave(ParkScene& scene, const ui::StringTable& strings, const ui::Palette& palette);

    /// Asks whether to pull `refs` down, naming them; the answer comes back in the park's update.
    void ask_demolish(ParkScene& scene, const ui::StringTable& strings, const ui::Palette& palette, std::vector<rct3::park::AttractionRef> refs);

    /// Asks whether to sell the pool complex the changing room `room` opens on.
    void ask_sell_pool_complex(ParkScene& scene, const ui::StringTable& strings, const ui::Palette& palette, std::size_t room);

} // namespace rct3::scene::park
