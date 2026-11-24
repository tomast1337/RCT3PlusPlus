// SPDX-License-Identifier: GPL-3.0-or-later
//
// Changes the player makes to the park's people from their pages and the staff window: hiring and firing
// staff, renaming people, painting their clothes and the rest a person's page sets. The people's meshes are
// built beside the frame, so a change drops that build first.
#pragma once

#include "config/game/options.hpp"
#include "core/types.hpp"
#include "save/model/people.hpp"
#include "simulation/components.hpp"
#include "ui/interface_assets.hpp"
#include "ui/screens/info/panel.hpp"

#include <cstddef>
#include <optional>
#include <string_view>

namespace rct3::scene::park
{

    struct ParkScene;

    /// After a person of `shown_people` is renamed `name`: a cheat's name sets it off while the player's
    /// `options` allow cheats (simulation/name_cheats.hpp), the guest camera's putting the view with them, from their eyes.
    void name_cheats(ParkScene& scene, const ui::InterfaceAssets& assets, const config::game::GameOptions& options, usize shown, std::u16string_view name);

    /// Writes a change the player made on a person's page into the save, a worker's by an action. Returns whether
    /// the page is to be shown again for its new look, or for a new name, its cheats looked for.
    [[nodiscard]] bool change_person(ParkScene& scene, usize shown, const ui::screens::info::InfoChange& change);

    /// Paints the clothes of pot `pot` of the person shown as `shown` in colour `colour`.
    void paint_person(ParkScene& scene, usize shown, usize pot, usize colour);

    /// Lets the member of staff shown as `shown` of shown_people go as the park runs, quitting or no longer needed:
    /// they leave the park at once, as one sacked does.
    void fire(ParkScene& scene, usize shown);

    /// Builds again the people actions applied have changed (simulation::actions::Changed): the shown people of the
    /// workers sacked go, those hired are shown and the last handed to the pointer, and those dressed anew wear it.
    /// Empties what it has taken.
    void show_changed_people(ParkScene& scene);

} // namespace rct3::scene::park
