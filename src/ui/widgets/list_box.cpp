// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/list_box.hpp"

#include "ui/text_layout.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::ui
{

    namespace
    {

        char32_t fold_case(char32_t c)
        {
            return c >= U'A' && c <= U'Z' ? c - U'A' + U'a' : c;
        }

    } // namespace

    ListBox::ListBox(std::string name, ListBoxStyle style, ScrollBarStyle bar)
        : Widget(std::move(name))
        , style(std::move(style))
    {
        const float across = bar.thickness;
        bar_ = &add<ScrollBar>("bar", std::move(bar), Orientation::Vertical);
        // Inside the padding, like the rows, so the bar stays off the frame.
        const Rect& pad = this->style.padding;
        bar_->placement = { { 1, 0, 1, 1 }, { -across - pad.x1, pad.y0, -pad.x1, -pad.y1 } };
    }

    void ListBox::select(std::optional<std::size_t> index)
    {
        selected_ = index && *index < items.size() ? index : std::nullopt;
        reveal_ = selected_.has_value();
    }

    void ListBox::pick(std::size_t index)
    {
        if (items.empty())
            return;
        index = std::min(index, items.size() - 1);
        if (selected_ != index)
            ++changes_;
        selected_ = index;
        reveal_ = true;
    }

    bool ListBox::take_change()
    {
        if (changes_ == 0)
            return false;
        --changes_;
        return true;
    }

    bool ListBox::take_activate()
    {
        if (activations_ == 0)
            return false;
        --activations_;
        return true;
    }

    Rect ListBox::rows_box() const
    {
        const Rect& r = rect();
        return { r.x0 + style.padding.x0, r.y0 + style.padding.y0, r.x1 - bar_->style().thickness - style.padding.x1, r.y1 - style.padding.y1 };
    }

    std::size_t ListBox::rows_in_view() const
    {
        return static_cast<std::size_t>(std::max(1.0f, std::floor(rows_box().height() / style.row_height)));
    }

    std::optional<std::size_t> ListBox::row_at(Point p) const
    {
        const Rect box = rows_box();
        if (!contains(box, p) || style.row_height <= 0)
            return std::nullopt;
        const auto row = static_cast<std::size_t>((p.y - box.y0 + bar_->position) / style.row_height);
        if (row >= items.size())
            return std::nullopt;
        return row;
    }

    void ListBox::on_press(Point p)
    {
        const auto row = row_at(p);
        if (!row)
            return;
        const bool again = last_pressed_ == row && selected_ == row && since_press_ <= style.double_click;
        pick(*row);
        if (again)
        {
            ++activations_;
            last_pressed_.reset(); // A third press starts again
        }
        else
        {
            last_pressed_ = row;
        }
        since_press_ = 0;
    }

    void ListBox::on_drag(Point p)
    {
        // Above or below the rows, the selection follows the pointer to the nearest row.
        const Rect box = rows_box();
        const Point inside { std::clamp(p.x, box.x0, box.x1 - 1), std::clamp(p.y, box.y0, box.y1 - 1) };
        if (const auto row = row_at(inside); row && row != selected_)
        {
            pick(*row);
            last_pressed_.reset();
        }
    }

    bool ListBox::on_wheel(Point notches)
    {
        return bar_->on_wheel(notches);
    }

    bool ListBox::on_key(const KeyPress& key)
    {
        if (items.empty())
            return false;
        const std::size_t last = items.size() - 1;
        const std::size_t page = std::max<std::size_t>(1, rows_in_view() - 1);
        const std::size_t at = selected_.value_or(0);
        switch (key.key)
        {
            case Key::Up:
                pick(selected_ ? (at == 0 ? 0 : at - 1) : last);
                return true;
            case Key::Down:
                pick(selected_ ? std::min(at + 1, last) : 0);
                return true;
            case Key::PageUp:
                pick(at > page ? at - page : 0);
                return true;
            case Key::PageDown:
                pick(std::min(at + page, last));
                return true;
            case Key::Home:
                pick(0);
                return true;
            case Key::End:
                pick(last);
                return true;
            case Key::Enter:
                if (!selected_)
                    return false;
                ++activations_;
                return true;
            case Key::Character:
            {
                const char32_t wanted = fold_case(key.character);
                for (std::size_t n = 1; n <= items.size(); ++n)
                {
                    const std::size_t i = (selected_ ? at + n : n - 1) % items.size();
                    if (!items[i].empty() && fold_case(items[i].front()) == wanted)
                    {
                        pick(i);
                        break;
                    }
                }
                return true;
            }
            case Key::Space:
                return true; // Typed as a character too
            default:
                return false;
        }
    }

    void ListBox::update(const UpdateContext& context)
    {
        since_press_ += context.elapsed;
        if (selected_ && *selected_ >= items.size())
            selected_.reset();

        const Rect box = rows_box();
        bar_->visible = box.height();
        bar_->content = std::max(box.height(), static_cast<float>(items.size()) * style.row_height);
        bar_->clamp();
        if (reveal_ && selected_)
        {
            const float top = static_cast<float>(*selected_) * style.row_height;
            if (top < bar_->position)
                bar_->position = top;
            else if (top + style.row_height > bar_->position + box.height())
                bar_->position = top + style.row_height - box.height();
            bar_->clamp();
        }
        reveal_ = false;
    }

    void ListBox::draw(DrawContext& context) const
    {
        const std::string& back = has_keyboard_focus() && !style.focused.empty() ? style.focused : style.background;
        if (!back.empty())
            draw_skin(context.list, context.skins.find(back), rect());
        const Font* f = context.fonts.get(style.font);
        const Rect box = rows_box();
        if (context.list.push_clip(box) && style.row_height > 0)
        {
            const auto first = static_cast<std::size_t>(bar_->position / style.row_height);
            for (std::size_t i = first; i < items.size(); ++i)
            {
                const float y = std::round(box.y0 + static_cast<float>(i) * style.row_height - bar_->position);
                if (y >= box.y1)
                    break;
                const Rect row { box.x0, y, box.x1, y + style.row_height };
                if (selected_ == i)
                {
                    if (const SkinRef* s = style.selection_skin.empty() ? nullptr : context.skins.find(style.selection_skin))
                        draw_skin(context.list, s, row);
                    else
                        context.list.fill(row, style.selection_colour);
                }
                if (f != nullptr)
                {
                    const Rect text { row.x0 + style.text_indent, row.y0, row.x1, row.y1 };
                    draw_text(context.list, *f, items[i], text, { enabled ? style.text_colour : style.disabled_text_colour, Align::Start, Align::Centre, false });
                }
            }
        }
        context.list.pop_clip();
    }

} // namespace rct3::ui
