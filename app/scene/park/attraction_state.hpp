// SPDX-License-Identifier: GPL-3.0-or-later
//
// Opening and closing the park's attractions: the states an attraction's tab offers, why one cannot be put in a
// state (a ride by its track and doors, a stall by its menu), and putting it there with the running park.
#pragma once

#include "park/attraction_objects.hpp"
#include "save/model/rides.hpp"
#include "ui/screens/attraction_info.hpp"
#include "ui/screens/info_panel.hpp"
#include "ui/string_table.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace rct3::scene::park
{

    struct ParkScene;

    /// Puts an attraction in `state`, the running park with it: open lets guests on, testing runs it empty.
    void set_attraction_state(ParkScene& scene, save::model::AttractionCommon& attraction, save::model::AttractionState state);

    /// Why attraction `ref` cannot be put in `state`, as a string key: a ride by its track and doors, a stall when
    /// its menu is empty; nothing when it can.
    [[nodiscard]] std::optional<std::string> state_refusal(const ParkScene& scene, rct3::park::AttractionRef ref, save::model::AttractionState state);
    /// The states the open-or-close tab of `ref` offers, in the order its choices fan out.
    [[nodiscard]] std::vector<ui::screens::StateChoice> state_choices(const ParkScene& scene, rct3::park::AttractionRef ref);
    /// Those choices as the tab fans them: each greyed, with why as its help, when `state_refusal` refuses it.
    [[nodiscard]] std::vector<ui::screens::InfoChoice> state_fan(const ParkScene& scene, rct3::park::AttractionRef ref, const ui::StringTable& strings);

    /// Puts ride `ride` in `state` when its track and doors let it; otherwise leaves it and gives the string
    /// key of why not.
    [[nodiscard]] std::optional<std::string> change_ride_state(ParkScene& scene, std::size_t ride, save::model::AttractionState state);
    /// The same for stall `stall`, which cannot open with nothing on its menu.
    [[nodiscard]] std::optional<std::string> change_stall_state(ParkScene& scene, std::size_t stall, save::model::AttractionState state);

    /// Has the running show ride that `show` is for put it on from its next go.
    void stage_show(ParkScene& scene, const save::model::RideShow& show);

} // namespace rct3::scene::park
