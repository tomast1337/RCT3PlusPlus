// SPDX-License-Identifier: GPL-3.0-or-later
//
// A single line of text the user can type into. Pressing it gives it the keyboard focus and puts the caret
// where it was pressed; dragging selects. While it has the focus:
//
// - typed characters replace the selection or go in at the caret, as far as max_length and `allowed` let them;
// - Left and Right move by a character, with Control by a word; Home and End go to the ends; Shift with any
//   of them extends the selection, and Control+A selects everything;
// - Backspace and Delete remove the selection, or a character (with Control, a word) before or after the
//   caret;
// - Control+C and Control+X copy and cut the selection, Control+V pastes, with line breaks turned to spaces
//   and the same limits as typing (also Control+Insert, Shift+Delete and Shift+Insert);
// - Enter commits: the owner asks with take_commit(). Escape, Tab, Up and Down are left to the parents.
//
// The caret blinks, half a second on and half off, and shows again at once after each change. Text wider
// than the box scrolls sideways to keep the caret in view.
//
// Text an input method is composing shows at the caret on a tinted, underlined patch, with the caret at the
// input method's cursor; it becomes part of `text` only when it arrives typed.
//
// Positions are UTF-16 indices into `text`, always at character boundaries. The owner may change `text`;
// the caret and selection are kept inside it.
#pragma once

#include "ui/widget.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace rct3::ui
{

    class EditBox : public Widget
    {
    public:
        explicit EditBox(std::string name, std::u16string text = { });

        std::u16string text;
        std::string skin;         ///< Stretched behind the text; none when empty
        std::string focused_skin; ///< Instead of `skin` while the box has the keyboard focus
        FontRole font = FontRole::Small;
        Color text_colour = hex("#000000");
        Color disabled_text_colour = hex("#7f7f7f");
        Color caret_colour = hex("#000000");
        Color selection_colour = hex("#007fff50");
        Color composition_colour = hex("#007fff33"); ///< Behind text being composed
        Rect padding { 3, 2, 3, 2 };                 ///< Between the edges and the text
        std::size_t max_length = 0;                  ///< In characters; 0 for no limit
        std::u16string allowed;                      ///< Characters that may be typed; any printable one when empty
        bool upper_case = false;                     ///< Typed letters a to z become capitals

        [[nodiscard]] std::size_t caret() const
        {
            return caret_;
        }
        /// The other end of the selection; equal to caret() when nothing is selected.
        [[nodiscard]] std::size_t anchor() const
        {
            return anchor_;
        }
        [[nodiscard]] bool has_selection() const
        {
            return caret_ != anchor_;
        }
        /// Moves the caret to `position` (snapped to the text), selecting from the anchor when `extend`.
        void set_caret(std::size_t position, bool extend = false);
        void select_all();

        /// True once for each Enter since the last call.
        [[nodiscard]] bool take_commit();
        /// True once for each change to the text by the user since the last call.
        [[nodiscard]] bool take_change();

        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        [[nodiscard]] bool takes_focus() const override
        {
            return true;
        }
        void on_press(Point p) override;
        void on_drag(Point p) override;
        bool on_key(const KeyPress& key) override;
        void on_composition(const Composition& composition) override;
        [[nodiscard]] const Composition& composition() const
        {
            return composition_;
        }
        void update(const UpdateContext& context) override;
        void draw(DrawContext& context) const override;

        /// The text between the caret and the anchor.
        [[nodiscard]] std::u16string selected_text() const;
        [[nodiscard]] std::optional<std::u16string> take_copied() override;

    private:
        /// `typed` without what the box does not take, with upper_case applied.
        [[nodiscard]] std::u16string accepted(std::u16string_view typed) const;
        void insert(std::u16string_view typed);
        void erase_selection();
        void changed();
        [[nodiscard]] Rect text_box() const;

        std::size_t caret_ = 0;
        std::size_t anchor_ = 0;
        float scroll_ = 0;     // Units the text is moved left by
        float blink_time_ = 0; // Since the last change or caret move
        bool pointer_pending_ = false;
        bool pointer_extends_ = false; // A drag, not a press
        Point pointer_;                // Placed at the next update, which has the font
        int commits_ = 0;
        int changes_ = 0;
        std::optional<std::u16string> copied_;
        Composition composition_; // Cleared when the focus goes
    };

} // namespace rct3::ui
