// SPDX-License-Identifier: GPL-3.0-or-later
//
// The buttons and keys a build tool reads each frame, beside where the pointer is.
#pragma once

namespace rct3::scene
{

    /// What the player holds down this frame, as a build tool sees it. Built at the call with designated
    /// initializers, so each flag is named where it is passed.
    struct ToolInput
    {
        bool down = false;           ///< The button that works the tool is held
        bool over_interface = false; ///< The pointer is on a window, or the view rides along, so not on the park
        bool shift = false;          ///< Either Shift key is held
        bool control = false;        ///< Either Control key is held
        bool cancel = false;         ///< The right button, which calls off the stroke or drag going on
    };

} // namespace rct3::scene
