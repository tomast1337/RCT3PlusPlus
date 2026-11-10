// SPDX-License-Identifier: GPL-3.0-or-later
//
// Changes the player makes to the park's people from their pages and the staff window: hiring and firing
// staff, renaming people, painting their clothes and the rest a person's page sets. The people's meshes are
// built beside the frame, so a change drops that build first.
#pragma once

#include "save/model/people.hpp"
#include "simulation/components.hpp"
#include "ui/interface_assets.hpp"
#include "ui/screens/info_panel.hpp"

#include <cstddef>
#include <optional>
#include <string_view>

namespace rct3::scene::park
{

    struct ParkScene;

    /// What the saved worker's window shows of `staff`'s pay, training, mood and work done.
    void copy_mood(const simulation::Staff& staff, save::model::Staff& saved);

    /// Hires a worker for job `kind` from the staff window and returns their place in shown_people, to be
    /// handed to the pointer as the game hands them; nothing where none can be hired. A keeper goes to an
    /// animal house picked at random instead, which dresses them with the other keepers.
    [[nodiscard]] std::optional<std::size_t> hire(ParkScene& scene, save::model::StaffKind kind);

    /// After a person of `shown_people` is renamed `name`: the name the game keeps for its guest camera cheat
    /// puts the view with them, from their eyes.
    void name_cheats(ParkScene& scene, const ui::InterfaceAssets& assets, std::size_t shown, std::u16string_view name);

    /// Writes a change the player made on a person's page into the save. Returns whether the page is to be
    /// shown again for its new look.
    [[nodiscard]] bool change_person(ParkScene& scene, std::size_t shown, const ui::screens::InfoChange& change);

    /// Paints the clothes of pot `pot` of the person shown as `shown` in colour `colour`.
    void paint_person(ParkScene& scene, std::size_t shown, std::size_t pot, std::size_t colour);

    /// Fires the member of staff shown as `shown` of shown_people: they leave the park at once, as in the game.
    void fire(ParkScene& scene, std::size_t shown);

} // namespace rct3::scene::park
