// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/edit_box.hpp"

#include "strings/unicode.hpp"
#include "ui/text_layout.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace rct3::ui
{

    namespace
    {

        using strings::is_high_surrogate;
        using strings::is_low_surrogate;

        constexpr float kBlinkPeriod = 1.0f; ///< Seconds; the caret shows for the first half
        constexpr float kCaretHalfWidth = 1.0f;

        // The position `at` moved back to the start of the character it falls in.
        std::size_t boundary(const std::u16string& s, std::size_t at)
        {
            at = std::min(at, s.size());
            if (at > 0 && at < s.size() && is_low_surrogate(s[at]) && is_high_surrogate(s[at - 1]))
                --at;
            return at;
        }

        std::size_t next_char(const std::u16string& s, std::size_t at)
        {
            if (at >= s.size())
                return s.size();
            return boundary(s, at + (is_high_surrogate(s[at]) ? 2 : 1));
        }

        std::size_t previous_char(const std::u16string& s, std::size_t at)
        {
            return at == 0 ? 0 : boundary(s, at - 1);
        }

        bool is_space(char16_t c)
        {
            return c == u' ' || c == u'\t';
        }

        // Past the spaces, then past the word: where Control+Right stops.
        std::size_t next_word(const std::u16string& s, std::size_t at)
        {
            while (at < s.size() && is_space(s[at]))
                ++at;
            while (at < s.size() && !is_space(s[at]))
                at = next_char(s, at);
            return at;
        }

        std::size_t previous_word(const std::u16string& s, std::size_t at)
        {
            while (at > 0 && is_space(s[at - 1]))
                --at;
            while (at > 0 && !is_space(s[at - 1]))
                at = previous_char(s, at);
            return at;
        }

        std::size_t count_chars(const std::u16string& s)
        {
            std::size_t n = 0;
            for (std::size_t i = 0; i < s.size(); i = next_char(s, i))
                ++n;
            return n;
        }

        // The character position nearest to `x` units from the start of the text.
        std::size_t position_at(const Font& font, const std::u16string& s, float x)
        {
            float left = 0;
            for (std::size_t i = 0; i < s.size();)
            {
                const std::size_t next = next_char(s, i);
                const float right = text_width(font, std::u16string_view(s).substr(0, next));
                if (x < (left + right) / 2)
                    return i;
                left = right;
                i = next;
            }
            return s.size();
        }

    } // namespace

    EditBox::EditBox(std::string name, std::u16string text)
        : Widget(std::move(name))
        , text(std::move(text))
    {
        caret_ = anchor_ = this->text.size();
    }

    void EditBox::set_caret(std::size_t position, bool extend)
    {
        caret_ = boundary(text, position);
        if (!extend)
            anchor_ = caret_;
        blink_time_ = 0;
    }

    void EditBox::select_all()
    {
        anchor_ = 0;
        caret_ = text.size();
        blink_time_ = 0;
    }

    bool EditBox::take_commit()
    {
        if (commits_ == 0)
            return false;
        --commits_;
        return true;
    }

    bool EditBox::take_change()
    {
        if (changes_ == 0)
            return false;
        --changes_;
        return true;
    }

    void EditBox::on_press(Point p)
    {
        pointer_ = p;
        pointer_pending_ = true;
        pointer_extends_ = false;
    }

    void EditBox::on_drag(Point p)
    {
        pointer_ = p;
        // A press not yet placed stays a press, so the selection starts where it went down.
        pointer_extends_ = !pointer_pending_ || pointer_extends_;
        pointer_pending_ = true;
    }

    void EditBox::erase_selection()
    {
        const std::size_t from = std::min(caret_, anchor_);
        const std::size_t to = std::max(caret_, anchor_);
        text.erase(from, to - from);
        caret_ = anchor_ = from;
    }

    void EditBox::changed()
    {
        ++changes_;
        blink_time_ = 0;
    }

    std::u16string EditBox::accepted(std::u16string_view typed) const
    {
        std::u16string out;
        for (std::size_t i = 0; i < typed.size(); ++i)
        {
            char32_t c = typed[i];
            if (is_high_surrogate(c) && i + 1 < typed.size() && is_low_surrogate(typed[i + 1]))
                c = 0x10000 + ((c - 0xD800) << 10) + (typed[++i] - 0xDC00);
            if (c < 0x20 || c == 0x7F || c > 0x10FFFF || is_high_surrogate(c) || is_low_surrogate(c))
                continue;
            if (upper_case && c >= U'a' && c <= U'z')
                c = c - U'a' + U'A';
            std::u16string piece;
            strings::append_utf16(piece, c);
            if (allowed.empty() || allowed.find(piece) != std::u16string::npos)
                out += piece;
        }
        return out;
    }

    void EditBox::insert(std::u16string_view typed)
    {
        std::u16string piece = accepted(typed);
        const bool replacing = has_selection();
        if (piece.empty() && !replacing)
            return;
        if (replacing)
            erase_selection();
        if (max_length != 0)
        {
            // Keep as many whole characters as there is room for.
            std::size_t room = max_length - std::min(max_length, count_chars(text));
            std::size_t end = 0;
            while (end < piece.size() && room > 0)
            {
                end = next_char(piece, end);
                --room;
            }
            piece.resize(end);
        }
        text.insert(caret_, piece);
        caret_ = anchor_ = caret_ + piece.size();
        if (replacing || !piece.empty())
            changed();
    }

    std::u16string EditBox::selected_text() const
    {
        const std::size_t from = std::min(caret_, anchor_);
        return text.substr(from, std::max(caret_, anchor_) - from);
    }

    std::optional<std::u16string> EditBox::take_copied()
    {
        return std::exchange(copied_, std::nullopt);
    }

    bool EditBox::on_key(const KeyPress& key)
    {
        caret_ = boundary(text, caret_);
        anchor_ = boundary(text, anchor_);
        switch (key.key)
        {
            case Key::Character:
            {
                std::u16string typed;
                if (key.character <= 0x10FFFF)
                    strings::append_utf16(typed, key.character);
                insert(typed);
                return true;
            }
            case Key::Paste:
                // Lines are joined with spaces: this box holds one.
                {
                    std::u16string pasted = key.text;
                    std::ranges::replace(pasted, u'\n', u' ');
                    std::erase(pasted, u'\r');
                    insert(pasted);
                }
                return true;
            case Key::Copy:
            case Key::Cut:
                if (has_selection())
                {
                    copied_ = selected_text();
                    if (key.key == Key::Cut)
                    {
                        erase_selection();
                        changed();
                    }
                }
                return true;
            case Key::Space:
                return true; // Typed as a character too
            case Key::Left:
                if (has_selection() && !key.shift && !key.control)
                    set_caret(std::min(caret_, anchor_));
                else
                    set_caret(key.control ? previous_word(text, caret_) : previous_char(text, caret_), key.shift);
                return true;
            case Key::Right:
                if (has_selection() && !key.shift && !key.control)
                    set_caret(std::max(caret_, anchor_));
                else
                    set_caret(key.control ? next_word(text, caret_) : next_char(text, caret_), key.shift);
                return true;
            case Key::Home:
                set_caret(0, key.shift);
                return true;
            case Key::End:
                set_caret(text.size(), key.shift);
                return true;
            case Key::Backspace:
            case Key::Delete:
            {
                if (!has_selection())
                {
                    const bool back = key.key == Key::Backspace;
                    if (back)
                        anchor_ = key.control ? previous_word(text, caret_) : previous_char(text, caret_);
                    else
                        anchor_ = key.control ? next_word(text, caret_) : next_char(text, caret_);
                }
                if (has_selection())
                {
                    erase_selection();
                    changed();
                }
                return true;
            }
            case Key::Enter:
                ++commits_;
                return true;
            case Key::Shortcut:
                if (key.character != U'a')
                    return false;
                select_all();
                return true;
            default:
                return false;
        }
    }

    Rect EditBox::text_box() const
    {
        const Rect& r = rect();
        return { r.x0 + padding.x0, r.y0 + padding.y0, r.x1 - padding.x1, r.y1 - padding.y1 };
    }

    void EditBox::update(const UpdateContext& context)
    {
        blink_time_ = std::fmod(blink_time_ + context.elapsed, kBlinkPeriod);
        caret_ = boundary(text, caret_);
        anchor_ = boundary(text, anchor_);
        const Font* f = context.fonts != nullptr ? context.fonts->get(font) : nullptr;
        if (f == nullptr)
            return;
        const Rect box = text_box();
        if (pointer_pending_)
        {
            set_caret(position_at(*f, text, pointer_.x - box.x0 + scroll_), pointer_extends_);
            pointer_pending_ = false;
        }

        // Keep the caret in view, and no empty room on the right while the text is wider than the box.
        const float caret_x = text_width(*f, std::u16string_view(text).substr(0, caret_));
        const float room = std::max(0.0f, box.width() - 2 * kCaretHalfWidth);
        scroll_ = std::clamp(scroll_, caret_x - room, caret_x);
        scroll_ = std::clamp(scroll_, 0.0f, std::max(0.0f, text_width(*f, text) - room));
    }

    void EditBox::draw(DrawContext& context) const
    {
        const std::string& back = has_keyboard_focus() && !focused_skin.empty() ? focused_skin : skin;
        if (!back.empty())
            draw_skin(context.list, context.skins.find(back), rect());
        const Font* f = context.fonts.get(font);
        if (f == nullptr)
            return;
        const Rect box = text_box();
        if (context.list.push_clip(box))
        {
            const float top = std::round(box.y0 + (box.height() - f->line_height()) / 2);
            const float origin = box.x0 + kCaretHalfWidth - scroll_;
            const auto x_of = [&](std::size_t at) { return std::round(origin + text_width(*f, std::u16string_view(text).substr(0, at))); };
            if (has_selection())
                context.list.fill({ x_of(std::min(caret_, anchor_)), top, x_of(std::max(caret_, anchor_)), top + f->line_height() }, selection_colour);
            const Rect line { origin, top, origin + text_width(*f, text) + 1, top + f->line_height() };
            draw_text(context.list, *f, text, line, { enabled ? text_colour : disabled_text_colour, Align::Start, Align::Start, false });
            if (has_keyboard_focus() && blink_time_ < kBlinkPeriod / 2)
            {
                const float x = x_of(caret_);
                context.list.fill({ x - kCaretHalfWidth, top, x + kCaretHalfWidth, top + f->line_height() }, caret_colour);
            }
        }
        context.list.pop_clip();
    }

} // namespace rct3::ui
