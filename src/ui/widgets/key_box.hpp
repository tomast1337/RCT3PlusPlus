// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shows the key combinations a command is on and lets the user set another: a click, or Enter or Space
// while it has the keyboard focus, makes it wait for keys, showing `prompt`. The next combination that can be
// bound (see key_combo.hpp) becomes its only one; Escape alone, or losing the focus, stops waiting and keeps
// the old ones. While waiting it takes every key, Tab too, so the keys pressed do nothing else; the owner
// should then give it the keys as pressed, not as a key map turns them into commands.
#pragma once

#include "ui/key_combo.hpp"
#include "ui/widget.hpp"

#include <string>
#include <vector>

namespace rct3::ui
{

    class KeyBox : public Widget
    {
    public:
        KeyBox(std::string name, std::vector<KeyCombo> combos);

        std::vector<KeyCombo> combos;
        std::string skin;         ///< Stretched behind the text; none when empty
        std::string focused_skin; ///< Instead of `skin` while the box has the keyboard focus
        std::u16string prompt;    ///< Shown while waiting for keys
        std::u16string none;      ///< Shown when there are no combinations
        FontRole font = FontRole::Small;
        Color text_colour { 0, 0, 0, 255 };
        Color disabled_text_colour { 127, 127, 127, 255 };
        Rect padding { 3, 2, 3, 2 }; ///< Between the edges and the text

        [[nodiscard]] bool waiting() const
        {
            return waiting_;
        }
        /// True once for each new combination since the last call.
        [[nodiscard]] bool take_change();

        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        [[nodiscard]] bool takes_focus() const override
        {
            return true;
        }
        void on_release(Point p, bool inside) override;
        bool on_key(const KeyPress& key) override;
        void update(const UpdateContext& context) override;
        void draw(DrawContext& context) const override;

    private:
        bool waiting_ = false;
        int changes_ = 0;
    };

} // namespace rct3::ui
