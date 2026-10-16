// SPDX-License-Identifier: GPL-3.0-or-later
//
// A whole number in a field with an up and a down arrow beside it. Pressing an arrow moves the value by a
// step and, held, keeps stepping. The number can also be typed: it is read back on Enter or when the field
// loses the keyboard focus, kept in range, and the field shows the result.
//
// With the focus in the field, Up and Down step, Page Up and Page Down move ten steps. The wheel over the
// spinner steps too, away from the user up.
//
// The owner asks with take_change() whether the user changed the value.
#pragma once

#include "ui/widgets/edit_box.hpp"

#include <cstdint>
#include <string>

namespace rct3::ui
{

    struct SpinnerStyle
    {
        std::string field;         ///< Behind the number
        std::string field_focused; ///< Behind it while typing
        std::string up;            ///< Family base name of the upper arrow (see SkinLibrary)
        std::string down;
        float arrow_width = 20;
        float repeat_delay = 0.4f; ///< Seconds an arrow is held before it repeats
        float repeat_interval = 0.08f;
    };

    class Spinner : public Widget
    {
    public:
        Spinner(std::string name, SpinnerStyle style);

        int minimum = 0;
        int maximum = 100;
        int step = 1;
        /// Places after the point the value is shown with: 2 shows 150 as 1.50, and reads 1.5 back as 150.
        int decimals = 0;

        [[nodiscard]] int value() const
        {
            return value_;
        }
        /// Sets the value, kept in range. Not reported by take_change().
        void set_value(int v);

        /// True once for each change by the user since the last call.
        [[nodiscard]] bool take_change();

        [[nodiscard]] EditBox& field()
        {
            return *field_;
        }
        [[nodiscard]] Rect up_arrow() const;
        [[nodiscard]] Rect down_arrow() const;

        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        void on_press(Point p) override;
        void on_release(Point p, bool inside) override;
        bool on_wheel(Point notches) override;
        bool on_key(const KeyPress& key) override;
        void update(const UpdateContext& context) override;
        void draw(DrawContext& context) const override;

    private:
        void step_by(std::int64_t steps); // By the user
        void read_field();
        void show_value();

        SpinnerStyle style_;
        EditBox* field_ = nullptr;
        int value_ = 0;
        int held_ = 0; // +1 while the up arrow is held, -1 for the down arrow
        float held_time_ = 0;
        float next_repeat_ = 0;
        bool typing_ = false; // The field has text not yet read back
        int changes_ = 0;
    };

} // namespace rct3::ui
