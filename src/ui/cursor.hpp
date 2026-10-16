// SPDX-License-Identifier: GPL-3.0-or-later
//
// The pointer's look, as the interface and the park ask for it. The game draws no pointer of its own: each
// frame the interface says which kind of pointer fits what is under it or what it is doing, and the
// platform shows the operating system's own cursor for that kind.
//
// Kinds are what the pointer means (a hand over something that clicks, a beam over text); shapes are the
// standard cursors a platform has. Not every platform has a shape for every kind, so each kind lists the
// shapes it may take, best first, and the platform shows the first one it has. The arrow is always there.
//
// A platform backend (GLFW today; SDL, Win32, Cocoa or Wayland later) only maps shapes to its own cursors:
// it says which shapes it has and shows one when asked (see CursorBackend). Choosing the kind, falling back
// and switching only when the shape changes are done here, the same for every backend.
#pragma once

#include <array>
#include <bitset>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace rct3::ui
{

    /// What the pointer means where it is.
    enum class CursorKind : std::uint8_t
    {
        Arrow,              ///< Nothing special
        Hand,               ///< Over something that clicks
        Text,               ///< Over text that can be typed in or selected
        Crosshair,          ///< Aiming a tool at a point in the park
        NotAllowed,         ///< Over something that would act but cannot now, such as a disabled button
        Move,               ///< Moving something in any direction, such as a window by its title
        ResizeHorizontal,   ///< Dragging a left or right edge
        ResizeVertical,     ///< Dragging a top or bottom edge
        ResizeDiagonalDown, ///< Dragging the top-left or bottom-right corner
        ResizeDiagonalUp,   ///< Dragging the top-right or bottom-left corner
        Grab,               ///< Over something that can be taken hold of and dragged
        Grabbing,           ///< Holding something and dragging it
        ZoomIn,
        ZoomOut,
        Hidden, ///< No pointer, held in place while the mouse moves, as for mouse look; it comes back where it was
    };
    inline constexpr std::size_t kCursorKinds = 15;

    /// For logs.
    [[nodiscard]] std::string_view cursor_name(CursorKind kind);

    /// The standard cursors platforms share. A backend shows each one as its own system cursor.
    enum class StandardCursor : std::uint8_t
    {
        Arrow,        ///< Every backend has it
        PointingHand, ///< As over a link
        IBeam,
        Crosshair,
        NotAllowed,
        ResizeAll,  ///< Arrows in four directions
        ResizeEW,   ///< Left and right
        ResizeNS,   ///< Up and down
        ResizeNWSE, ///< Top-left and bottom-right
        ResizeNESW, ///< Top-right and bottom-left
        Hidden,     ///< No cursor, the pointer held for relative motion; every backend has it
    };
    inline constexpr std::size_t kStandardCursors = 11;

    /// For logs.
    [[nodiscard]] std::string_view standard_cursor_name(StandardCursor shape);

    /// The standard cursors a backend has, by StandardCursor value.
    using StandardCursorSet = std::bitset<kStandardCursors>;

    /// Arrow and Hidden: what every backend must have, so that every kind has a shape.
    [[nodiscard]] StandardCursorSet required_cursors();

    /// The shapes `kind` may take, best first; the last is always one every backend has.
    [[nodiscard]] std::span<const StandardCursor> cursor_shapes(CursorKind kind);

    /// The first of `kind`'s shapes that `available` has.
    [[nodiscard]] StandardCursor resolve_cursor(CursorKind kind, const StandardCursorSet& available);

    /// What a platform backend provides: which standard cursors it has, and showing one of them.
    template<class Backend>
    concept CursorBackend = requires(Backend& backend, const Backend& shown, StandardCursor shape) {
        { shown.available() } -> std::same_as<StandardCursorSet>;
        backend.show(shape);
    };

    /// Shows the cursor for a kind through a backend, telling the backend only when the shape changes.
    class CursorSwitch
    {
    public:
        /// Shows `kind`'s shape. Returns the shape when it changed, nothing when it was already shown.
        template<CursorBackend Backend> std::optional<StandardCursor> set(Backend& backend, CursorKind kind)
        {
            const StandardCursor shape = resolve_cursor(kind, backend.available());
            if (shown_ == shape)
                return std::nullopt;
            backend.show(shape);
            shown_ = shape;
            return shape;
        }

        /// The shape last shown; nothing before the first set().
        [[nodiscard]] std::optional<StandardCursor> shown() const
        {
            return shown_;
        }

    private:
        std::optional<StandardCursor> shown_;
    };

} // namespace rct3::ui
