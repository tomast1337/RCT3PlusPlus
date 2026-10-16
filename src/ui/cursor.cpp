// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/cursor.hpp"

#include <cassert>

namespace rct3::ui
{

    namespace
    {

        using enum StandardCursor;

        struct KindShapes
        {
            std::string_view name;
            std::array<StandardCursor, 3> shapes; // Best first; repeats at the end where there are fewer
        };

        // The fallback table: for each kind, the standard shapes it may take, best first, ending with one every
        // backend has. Platforms have no open or closed hand, no magnifier and no "busy" shape in common, and
        // older ones lack the diagonal, four-way and not-allowed shapes, so:
        //
        //   kind                 best          then          last
        //   Arrow                Arrow
        //   Hand                 PointingHand                Arrow
        //   Text                 IBeam                       Arrow
        //   Crosshair            Crosshair                   Arrow
        //   NotAllowed           NotAllowed                  Arrow
        //   Move                 ResizeAll     PointingHand  Arrow
        //   ResizeHorizontal     ResizeEW                    Arrow
        //   ResizeVertical       ResizeNS                    Arrow
        //   ResizeDiagonalDown   ResizeNWSE    ResizeAll     Arrow
        //   ResizeDiagonalUp     ResizeNESW    ResizeAll     Arrow
        //   Grab                 PointingHand                Arrow   (no open hand: the hand that clicks is closest)
        //   Grabbing             ResizeAll     PointingHand  Arrow   (no closed hand: what is held moves every way)
        //   ZoomIn, ZoomOut      Crosshair                   Arrow   (no magnifier: it aims at where to zoom)
        //   Hidden               Hidden
        constexpr std::array<KindShapes, kCursorKinds> kKinds { {
            { "arrow", { Arrow, Arrow, Arrow } },
            { "hand", { PointingHand, Arrow, Arrow } },
            { "text", { IBeam, Arrow, Arrow } },
            { "crosshair", { Crosshair, Arrow, Arrow } },
            { "not-allowed", { NotAllowed, Arrow, Arrow } },
            { "move", { ResizeAll, PointingHand, Arrow } },
            { "resize-horizontal", { ResizeEW, Arrow, Arrow } },
            { "resize-vertical", { ResizeNS, Arrow, Arrow } },
            { "resize-diagonal-down", { ResizeNWSE, ResizeAll, Arrow } },
            { "resize-diagonal-up", { ResizeNESW, ResizeAll, Arrow } },
            { "grab", { PointingHand, Arrow, Arrow } },
            { "grabbing", { ResizeAll, PointingHand, Arrow } },
            { "zoom-in", { Crosshair, Arrow, Arrow } },
            { "zoom-out", { Crosshair, Arrow, Arrow } },
            { "hidden", { Hidden, Hidden, Hidden } },
        } };
        static_assert(static_cast<std::size_t>(CursorKind::Hidden) + 1 == kCursorKinds);
        static_assert(static_cast<std::size_t>(StandardCursor::Hidden) + 1 == kStandardCursors);

        constexpr std::array<std::string_view, kStandardCursors> kShapeNames { "arrow", "pointing-hand", "i-beam", "crosshair", "not-allowed", "resize-all", "resize-ew", "resize-ns", "resize-nwse", "resize-nesw", "hidden" };

        const KindShapes& entry(CursorKind kind)
        {
            return kKinds.at(static_cast<std::size_t>(kind));
        }

    } // namespace

    std::string_view cursor_name(CursorKind kind)
    {
        return entry(kind).name;
    }

    std::string_view standard_cursor_name(StandardCursor shape)
    {
        return kShapeNames.at(static_cast<std::size_t>(shape));
    }

    StandardCursorSet required_cursors()
    {
        StandardCursorSet set;
        set.set(static_cast<std::size_t>(Arrow));
        set.set(static_cast<std::size_t>(Hidden));
        return set;
    }

    std::span<const StandardCursor> cursor_shapes(CursorKind kind)
    {
        return entry(kind).shapes;
    }

    StandardCursor resolve_cursor(CursorKind kind, const StandardCursorSet& available)
    {
        for (const StandardCursor shape : cursor_shapes(kind))
        {
            if (available.test(static_cast<std::size_t>(shape)))
                return shape;
        }
        // Only reached with a backend missing a required shape.
        assert(false && "a cursor backend must have the arrow and hidden shapes");
        return kind == CursorKind::Hidden ? Hidden : Arrow;
    }

} // namespace rct3::ui
