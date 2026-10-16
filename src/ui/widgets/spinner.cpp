// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/spinner.hpp"

#include "strings/unicode.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>

namespace rct3::ui
{

    namespace
    {

        constexpr std::int64_t kPageSteps = 10;
        constexpr std::size_t kMaxDigits = 12; ///< Enough for any int with its sign and a point

        std::u16string format(int v, int decimals)
        {
            std::string text = std::to_string(std::abs(static_cast<long long>(v)));
            if (decimals > 0)
            {
                const auto places = static_cast<std::size_t>(decimals);
                if (text.size() <= places)
                    text.insert(0, places + 1 - text.size(), '0');
                text.insert(text.size() - places, 1, '.');
            }
            return strings::to_utf16(v < 0 ? "-" + text : text);
        }

        /// `text` with its point taken out and `decimals` places after it, so it reads as a whole number;
        /// unchanged when it has no point. Empty when it has more places than that.
        std::string without_point(std::string text, int decimals)
        {
            const auto point = text.find('.');
            if (point == std::string::npos)
                return text + std::string(static_cast<std::size_t>(std::max(decimals, 0)), '0');
            const std::size_t places = text.size() - point - 1;
            if (decimals <= 0 || places > static_cast<std::size_t>(decimals))
                return { };
            text.erase(point, 1);
            return text + std::string(static_cast<std::size_t>(decimals) - places, '0');
        }

    } // namespace

    Spinner::Spinner(std::string name, SpinnerStyle style)
        : Widget(std::move(name))
        , style_(std::move(style))
    {
        field_ = &add<EditBox>("field");
        field_->skin = style_.field;
        field_->focused_skin = style_.field_focused;
        field_->placement = { { 0, 0, 1, 1 }, { 0, 0, -style_.arrow_width, 0 } };
        field_->allowed = u"-0123456789.";
        field_->max_length = kMaxDigits;
        show_value();
    }

    void Spinner::set_value(int v)
    {
        value_ = std::clamp(v, std::min(minimum, maximum), std::max(minimum, maximum));
        typing_ = false;
        show_value();
    }

    void Spinner::show_value()
    {
        const std::u16string text = format(value_, decimals);
        if (field_->text != text)
        {
            field_->text = text;
            field_->set_caret(text.size());
        }
    }

    bool Spinner::take_change()
    {
        if (changes_ == 0)
            return false;
        --changes_;
        return true;
    }

    void Spinner::step_by(std::int64_t steps)
    {
        read_field();
        const std::int64_t wanted = static_cast<std::int64_t>(value_) + steps * step;
        const int before = value_;
        set_value(static_cast<int>(std::clamp<std::int64_t>(wanted, std::numeric_limits<int>::min(), std::numeric_limits<int>::max())));
        if (value_ != before)
            ++changes_;
    }

    void Spinner::read_field()
    {
        if (!typing_)
            return;
        typing_ = false;
        const std::string digits = without_point(strings::to_utf8(field_->text), decimals);
        long long parsed = 0;
        const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), parsed);
        // Anything that is not a whole number puts the old value back.
        if (error == std::errc::result_out_of_range)
            parsed = digits.starts_with('-') ? std::numeric_limits<long long>::min() : std::numeric_limits<long long>::max();
        else if (error != std::errc { } || end != digits.data() + digits.size())
            parsed = value_;
        const int before = value_;
        set_value(static_cast<int>(std::clamp<long long>(parsed, std::numeric_limits<int>::min(), std::numeric_limits<int>::max())));
        if (value_ != before)
            ++changes_;
        show_value();
    }

    Rect Spinner::up_arrow() const
    {
        const Rect& r = rect();
        return { r.x1 - style_.arrow_width, r.y0, r.x1, std::round((r.y0 + r.y1) / 2) };
    }

    Rect Spinner::down_arrow() const
    {
        const Rect& r = rect();
        return { r.x1 - style_.arrow_width, std::round((r.y0 + r.y1) / 2), r.x1, r.y1 };
    }

    void Spinner::on_press(Point p)
    {
        held_ = contains(up_arrow(), p) ? 1 : contains(down_arrow(), p) ? -1 : 0;
        if (held_ == 0)
            return;
        step_by(held_);
        held_time_ = 0;
        next_repeat_ = style_.repeat_delay;
    }

    void Spinner::on_release(Point, bool)
    {
        held_ = 0;
    }

    bool Spinner::on_wheel(Point notches)
    {
        if (notches.y == 0)
            return false;
        step_by(static_cast<std::int64_t>(std::lround(notches.y)));
        return true;
    }

    bool Spinner::on_key(const KeyPress& key)
    {
        switch (key.key)
        {
            case Key::Up:
                step_by(1);
                return true;
            case Key::Down:
                step_by(-1);
                return true;
            case Key::PageUp:
                step_by(kPageSteps);
                return true;
            case Key::PageDown:
                step_by(-kPageSteps);
                return true;
            default:
                return false;
        }
    }

    void Spinner::update(const UpdateContext& context)
    {
        if (field_->take_change())
            typing_ = true;
        if (field_->take_commit() || (typing_ && !field_->has_keyboard_focus()))
        {
            read_field();
            field_->select_all();
        }
        if (!typing_)
            show_value();

        if (held_ != 0 && pressed())
        {
            held_time_ += context.elapsed;
            while (held_time_ >= next_repeat_)
            {
                step_by(held_);
                next_repeat_ += style_.repeat_interval;
            }
        }
    }

    void Spinner::draw(DrawContext& context) const
    {
        const auto arrow = [&](const std::string& skin, const Rect& box, int direction) {
            const bool at_end = direction > 0 ? value_ >= std::max(minimum, maximum) : value_ <= std::min(minimum, maximum);
            const SkinState state = !enabled || at_end ? SkinState::Disabled : held_ == direction && pressed() ? SkinState::Pressed : SkinState::Up;
            draw_skin_state(context.list, context.skins, skin, state, box);
        };
        arrow(style_.up, up_arrow(), 1);
        arrow(style_.down, down_arrow(), -1);
    }

} // namespace rct3::ui
