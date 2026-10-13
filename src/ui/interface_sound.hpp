// SPDX-License-Identifier: GPL-3.0-or-later
//
// The sounds the interface asks for, for its owner to play: a click when a button goes down and another when
// it comes back up over the button. Pressing outside an open popup, which closes it, sounds like a release.
// Hovering, opening and closing windows make no sound.
#pragma once

#include <cstdint>

namespace rct3::ui
{

    enum class InterfaceSound : std::uint8_t
    {
        Press,
        Release,
    };

} // namespace rct3::ui
