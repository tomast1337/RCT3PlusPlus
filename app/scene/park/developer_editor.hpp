// SPDX-License-Identifier: GPL-3.0-or-later
//
// The developer windows' editor (F12): a guest picked from a list and looked into, their feelings, needs and money
// changed or held, and the guest sent somewhere. Every change goes to the simulation, so a park saved afterwards
// keeps it.
#pragma once

#include "render/device.hpp"
#include "simulation/entt.hpp"
#include "ui/string_table.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace rct3::scene::park
{

    struct ParkScene;

    /// What the editor keeps between frames, with the park so scripts reach it too.
    struct DeveloperEditor
    {
        entt::entity guest = entt::null; ///< Picked in the list, marked in the park while the editor shows
        bool shown = false;              ///< Drawn this frame; main clears it before the developer windows
        bool focus = false;              ///< Brought in front of the windows docked with it when next drawn, as a guest was picked
        std::array<char, 64> search { };
        std::array<char, 64> name { };     ///< The rename box, filled from the guest when they are picked
        std::int32_t thought = 0;          ///< Picked to give: by ThoughtKind's number
        entt::entity send_to = entt::null; ///< Picked to send the guest to
        std::vector<entt::entity> rows;    ///< The guest list as shown, kept so its memory is reused
        /// The thoughts the language has words for: ThoughtKind's number and the words; read once.
        struct ThoughtText
        {
            std::int32_t id = 0;
            std::string text { };
        };
        std::vector<ThoughtText> thoughts;
    };

    /// Picks `guest` in the editor, filling its rename box, and brings the editor to the front.
    void pick_guest(ParkScene& scene, entt::entity guest);

    /// Draws the editor's tabs into the current developer window; the words of thoughts from `strings`.
    void draw_developer_editor(ParkScene& scene, const ui::StringTable& strings);

    /// Marks the picked guest over the game drawn in `view` (framebuffer pixels) while the editor shows.
    void mark_picked_guest(const ParkScene& scene, const render::ScreenView& view);

} // namespace rct3::scene::park
