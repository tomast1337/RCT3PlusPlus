// SPDX-License-Identifier: GPL-3.0-or-later
//
// The pointer's look over a park: from what the camera is doing, the tool in use, what is under the pointer
// and what the interface asks for. Kept apart from the scene so the choice can be checked without a park.
#pragma once

#include "ui/cursor.hpp"

#include <cstdint>
#include <optional>

namespace rct3::ui
{

    /// The kind of tool acting on the park, for its pointer.
    enum class PointerTool : std::uint8_t
    {
        None,   ///< Looking and clicking on things
        Place,  ///< Putting down an object held by the pointer
        Delete, ///< Removing what is clicked
        Land,   ///< Shaping or painting the ground
        Path,   ///< Laying paths and queues
        Water,  ///< Raising and lowering water
    };

    /// Where the pointer is over the park and what it is doing.
    struct ParkPointer
    {
        /// What the interface asks for; set when the pointer is over a window or a widget holds it.
        std::optional<CursorKind> interface;
        bool turning = false; ///< Turning and tilting the camera by the mouse
        bool panning = false; ///< Dragging the ground along
        int zooming = 0;      ///< Positive while zooming in from the keyboard, negative while zooming out
        PointerTool tool = PointerTool::None;
        bool over_pickable = false; ///< Something that opens a panel when clicked is under the pointer
        bool blocked = false;       ///< The tool cannot act where the pointer is
    };

    /// The pointer for `pointer`. Moving the camera by the mouse wins, as it goes on over windows; then the
    /// interface; then zooming; then the tool; then what is under the pointer.
    [[nodiscard]] CursorKind park_cursor(const ParkPointer& pointer);

} // namespace rct3::ui
