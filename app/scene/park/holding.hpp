// SPDX-License-Identifier: GPL-3.0-or-later
//
// Picking people up and putting them down elsewhere. The pick-up button on a guest's or a member of staff's
// page hands them to the pointer: they hang a few metres over the ground or path under it, and a click lets
// them go there, onto a path or the park's own land, to fall and walk on from the nearest path. Water and land
// the park does not own will not take them; a right click or Escape puts them back where they were picked up.
#pragma once

#include "simulation/entt.hpp"
#include "ui/widget.hpp"

#include <cstddef>
#include <glm/glm.hpp>
#include <optional>

namespace rct3::scene::park
{

    struct ParkScene;

    /// A person the player holds under the pointer.
    struct HeldPerson
    {
        entt::entity who = entt::null;
        glm::vec3 from { 0.0f };       ///< Where they go if given back: where picked up, or a hire to the entrance
        std::optional<glm::vec3> spot; ///< The ground or path under the pointer
        bool allowed = false;          ///< They may be let go there
    };

    /// Picks up the shown person `shown` (of ParkScene::shown_people). False when they cannot be picked up now:
    /// not walking in the park, or on a ride.
    bool pick_up_person(ParkScene& scene, std::size_t shown);

    /// Hangs the held person over what is under the pointer at `at` (interface units, on a view `size`
    /// across), and lets them go there on `click` if they may be, or back where they came from on `cancel`.
    /// Returns whether someone is held, so a click goes to nothing else.
    bool hold_person(ParkScene& scene, ui::Point at, ui::Point size, bool click, bool cancel);

} // namespace rct3::scene::park
