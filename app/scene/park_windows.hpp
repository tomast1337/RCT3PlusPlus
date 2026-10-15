// SPDX-License-Identifier: GPL-3.0-or-later
//
// The windows the park's Files menu opens over the park: the options, with what the front end's options
// offer (the language shows but cannot change while a park is open), and the about window. The options are
// the front end's own, so a change shows on both, and they are saved when the window closes or the park
// goes.
#pragma once

#include "scene/front_end_scene.hpp"
#include "scene/park_scene.hpp"

namespace rct3::scene
{

    /// Opens the windows the park asked for and gathers what the user picked. Call after updating the park.
    void update_park_windows(ParkScene& park, FrontEndScene& front);

    /// What the windows over the park want done to the window or the program, as the front end's requests.
    [[nodiscard]] FrontEndRequests take_park_window_requests(ParkScene& park, FrontEndScene& front);

    /// True while the options window over the park waits for keys, which should then do nothing else.
    [[nodiscard]] bool park_waiting_for_keys(const ParkScene& park);

    /// Closes the options window if it is open and saves the options; before the park goes.
    void close_park_windows(ParkScene& park, FrontEndScene& front);

} // namespace rct3::scene
