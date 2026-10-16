// SPDX-License-Identifier: GPL-3.0-or-later
//
// A horizontal slider: a thumb on a rail picks a value between `minimum` and `maximum`. Pressing the thumb
// drags it; pressing the rail elsewhere first puts the thumb's centre there, then drags it from there. Drawn
// as a bar instead, it fills from the left as far as the value, and a press anywhere sets the value there.
//
// Pressing the slider gives it the keyboard focus. Then Left and Down lower the value by a step, Right and
// Up raise it, Page Down and Page Up move a tenth of the range, and Home and End go to the ends. The wheel
// turned away from the user raises it. With a `step`, values snap to whole steps from the minimum; without
// one, the keys move a twentieth of the range.
//
// The owner asks with take_change() whether the user moved it.
#pragma once

#include "ui/widget.hpp"

#include <string>

namespace rct3::ui
{

    struct SliderStyle
    {
        std::string rail;  ///< Stretched along the track; a line of `rail_colour` when empty
        std::string thumb; ///< Family base name (see SkinLibrary), drawn at thumb_size
        Color rail_colour = hex("#286eaa");
        Color thumb_colour = hex("#3caa3c"); ///< When there is no thumb skin
        float rail_thickness = 3;
        float thumb_width = 18;
        float thumb_height = 13;
        bool bar = false; ///< A filled bar with no thumb, over the slider's whole rectangle; give thumb_width 0
        Color bar_colour = hex("#44c7ff");
        Color bar_back_colour = hex("#7f7f7f");
    };

    class Slider : public Widget
    {
    public:
        Slider(std::string name, SliderStyle style);

        SliderStyle style;
        float minimum = 0;
        float maximum = 1;
        float step = 0; ///< 0 for any value

        [[nodiscard]] float value() const
        {
            return value_;
        }
        /// Sets the value, snapped and kept in range. Not reported by take_change().
        void set_value(float v);

        /// True once for each move by the user since the last call.
        [[nodiscard]] bool take_change();

        /// Where the thumb is drawn now, in desktop units.
        [[nodiscard]] Rect thumb() const;

        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        [[nodiscard]] bool takes_focus() const override
        {
            return true;
        }
        /// An open hand over the thumb, which drags, and a hand over the rest, which moves the thumb there.
        [[nodiscard]] CursorKind cursor(Point p) const override;
        [[nodiscard]] CursorKind held_cursor(Point) const override
        {
            return CursorKind::Grabbing;
        }
        void on_press(Point p) override;
        void on_drag(Point p) override;
        bool on_wheel(Point notches) override;
        bool on_key(const KeyPress& key) override;
        void draw(DrawContext& context) const override;

    private:
        [[nodiscard]] float snapped(float v) const;
        void move_to(float v); // By the user
        [[nodiscard]] float key_step() const;

        float value_ = 0;
        float grab_ = 0; // Pointer offset from the thumb's centre while dragging
        int changes_ = 0;
    };

} // namespace rct3::ui
