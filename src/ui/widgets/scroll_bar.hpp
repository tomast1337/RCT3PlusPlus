// SPDX-License-Identifier: GPL-3.0-or-later
//
// A scroll bar: an arrow at each end, a rail between them and a rider on the rail whose length is the share
// of the content in view. The bar owns the scroll position; whatever it scrolls reads `position` back.
//
// An arrow moves by one step and, held, keeps stepping. Pressing the rider drags it. Pressing the rail
// elsewhere first centres the rider on the pointer, then drags it from there. The wheel moves by its own
// step. With nothing to scroll the rider is hidden and the bar ignores input.
#pragma once

#include "ui/widget.hpp"

#include <cstdint>
#include <string>

namespace rct3::ui
{

    enum class Orientation : std::uint8_t
    {
        Vertical,
        Horizontal,
    };

    struct ScrollBarStyle
    {
        std::string back_arrow;    ///< Up or left
        std::string forward_arrow; ///< Down or right
        std::string rail;          ///< Stretched along the track
        std::string rider;         ///< Family base name (see SkinLibrary)
        float thickness = 15;      ///< Across the bar; the arrows are squares of this size
        float step = 20;           ///< Per arrow press
        float wheel_step = 60;     ///< Per wheel notch
        float repeat_delay = 0.4f; ///< Seconds an arrow is held before it repeats
        float repeat_interval = 0.05f;
        float min_rider = 10; ///< So a long list keeps a rider that can be grabbed
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

        float content = 0;  ///< Length of what is scrolled
        float visible = 0;  ///< Length of the part in view
        float position = 0; ///< Of the view's start in the content, 0 to range()

        [[nodiscard]] Orientation orientation() const
        {
            return orientation_;
        }
        [[nodiscard]] const ScrollBarStyle& style() const
        {
            return style_;
        }
        /// How far the view can move: content - visible, at least 0.
        [[nodiscard]] float range() const;
        /// Moves the view by `delta`, kept within the range.
        void scroll_by(float delta);
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
        enum class Part : std::uint8_t
        {
            None,
            Back,
            Forward,
            Rider,
        };

        [[nodiscard]] float along(Point p) const;
        void drag_to(float pointer);

        ScrollBarStyle style_;
        Orientation orientation_;
        Part held_ = Part::None;
        float grab_ = 0; ///< Pointer offset from the rider's start while it is dragged
        float held_time_ = 0;
        float next_repeat_ = 0;
    };

} // namespace rct3::ui
