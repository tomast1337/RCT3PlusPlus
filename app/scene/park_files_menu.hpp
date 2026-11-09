// SPDX-License-Identifier: GPL-3.0-or-later
//
// The windows the park's Files menu opens over the park: the options, with what the front end's options
// offer (the language shows but cannot change while a park is open), the about window, and the windows that
// save and load games. The options are the front end's own, so a change shows on both, and they are saved
// when the window closes or the park goes.
//
// Save and load share one window, as in the game: opening the other kind replaces it. Both show the saved
// parks, ours and the original game's. Saving has nothing to write with yet, so its button stays greyed
// (and the game's question whether to save before loading is not asked). A park picked to load comes back
// with the requests, for the owner to close this park and load that one.
#pragma once

#include "scene/front_end_scene.hpp"
#include "scene/park_scene.hpp"

namespace rct3::scene
{

    /// Opens the windows the park asked for and gathers what the user picked. Call after updating the park.
    void update_park_windows(render::Device& device, ParkScene& park, FrontEndScene& front);

    /// What the windows over the park want done to the window or the program, as the front end's requests.
    [[nodiscard]] FrontEndRequests take_park_window_requests(ParkScene& park, FrontEndScene& front);

    /// True while the options window over the park waits for keys, which should then do nothing else.
    [[nodiscard]] bool park_waiting_for_keys(const ParkScene& park);

    /// Closes the options window if it is open and saves the options, and frees the save window's pictures;
    /// before the park goes.
    void close_park_windows(render::Device& device, ParkScene& park, FrontEndScene& front);

} // namespace rct3::scene
