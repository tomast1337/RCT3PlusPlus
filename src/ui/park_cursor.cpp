// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/park_cursor.hpp"

namespace rct3::ui
{

    namespace
    {

        CursorKind tool_cursor(PointerTool tool)
        {
            switch (tool)
            {
                case PointerTool::Place:
                    return CursorKind::Grab; // The object goes where the pointer holds it
                case PointerTool::Delete:
                case PointerTool::Land:
                case PointerTool::Path:
                case PointerTool::Water:
                    return CursorKind::Crosshair; // Aimed at a point of the ground
                case PointerTool::None:
                    break;
            }
            return CursorKind::Arrow;
        }

    } // namespace

    CursorKind park_cursor(const ParkPointer& pointer)
    {
        // Mouse look hides the pointer and holds it, so it does not run into the screen's edge.
        if (pointer.turning)
            return CursorKind::Hidden;
        if (pointer.panning)
            return CursorKind::Grabbing;
        if (pointer.interface)
            return *pointer.interface;
        if (pointer.zooming != 0)
            return pointer.zooming > 0 ? CursorKind::ZoomIn : CursorKind::ZoomOut;
        if (pointer.tool != PointerTool::None)
            return pointer.blocked ? CursorKind::NotAllowed : tool_cursor(pointer.tool);
        return pointer.over_pickable ? CursorKind::Hand : CursorKind::Arrow;
    }

} // namespace rct3::ui
