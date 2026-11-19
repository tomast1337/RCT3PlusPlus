// SPDX-License-Identifier: GPL-3.0-or-later
//
// A scroll bar: an arrow at each end, a rail between them and a rider on the rail whose length is the share
// of the content in view. The bar owns the scroll position; whatever it scrolls reads `position` back.
//
// An arrow moves by one step and, held, keeps stepping. Pressing the rider drags it. Pressing the rail
// elsewhere first centres the rider on the pointer, then drags it from there. The wheel moves by its own
// step. With nothing to scroll the rider is hidden and the bar ignores input.
#pragma once

#include "core/types.hpp"
#include "ui/widget.hpp"

#include <cstdint>
#include <string>

namespace rct3::ui::widgets
{

    enum class Orientation : u8
    {
        Vertical,
        Horizontal,
    };

    struct ScrollBarStyle
    {
        std::string back_arrow;               ///< Up or left
        std::string forward_arrow;            ///< Down or right
        std::string rail;                     ///< Stretched along the track
        std::string rider;                    ///< Family base name (see SkinLibrary)
        f32 thickness = 15;                   ///< Across the bar; the arrows are squares of this size
        f32 step = 20;                        ///< Per arrow press
        f32 wheel_step = 60;                  ///< Per wheel notch
        units::Seconds repeat_delay { 0.4f }; ///< An arrow is held this long before it repeats
        units::Seconds repeat_interval { 0.05f };
        f32 min_rider = 10; ///< So a long list keeps a rider that can be grabbed
    };

    /// Where the parts of a bar are, in desktop units.
    struct ScrollBarParts
    {
        Rect back;
        Rect forward;
        Rect track; ///< Between the arrows
        Rect rider; ///< Empty when there is nothing to scroll
    };

    class ScrollBar : public Widget
    {
    public:
        ScrollBar(std::string name, ScrollBarStyle style, Orientation orientation);

        f32 content = 0;  ///< Length of what is scrolled
        f32 visible = 0;  ///< Length of the part in view
        f32 position = 0; ///< Of the view's start in the content, 0 to range()

        [[nodiscard]] Orientation orientation() const
        {
            return orientation_;
        }
        [[nodiscard]] const ScrollBarStyle& style() const
        {
            return style_;
        }
        /// How far the view can move: content - visible, at least 0.
        [[nodiscard]] f32 range() const;
        /// Moves the view by `delta`, kept within the range.
        void scroll_by(f32 delta);
        /// Puts `position` back in the range, e.g. after the content shrank.
        void clamp();

        [[nodiscard]] ScrollBarParts parts() const;

        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        /// An open hand over the rider, which drags, a hand over the arrows and the rail, and the arrow with
        /// nothing to scroll.
        [[nodiscard]] CursorKind cursor(Point p) const override;
        [[nodiscard]] CursorKind held_cursor(Point p) const override;
        void on_press(Point p) override;
        void on_drag(Point p) override;
        void on_release(Point p, bool inside) override;
        /// Uses the wheel along the bar: y for a vertical bar, x for a horizontal one.
        bool on_wheel(Point notches) override;
        void update(const UpdateContext& context) override;
        void draw(DrawContext& context) const override;

    private:
        enum class Part : u8
        {
            None,
            Back,
            Forward,
            Rider,
        };

        [[nodiscard]] f32 along(Point p) const;
        void drag_to(f32 pointer);

        ScrollBarStyle style_;
        Orientation orientation_;
        Part held_ = Part::None;
        f32 grab_ = 0; ///< Pointer offset from the rider's start while it is dragged
        units::Seconds held_time_;
        units::Seconds next_repeat_;
    };

} // namespace rct3::ui::widgets
