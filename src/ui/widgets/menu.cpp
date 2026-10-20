// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/menu.hpp"

#include "ui/text_layout.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::ui
{

    Menu::Menu(std::string name, MenuStyle style, std::vector<MenuItem> items)
        : Widget(std::move(name))
        , style(std::move(style))
        , items(std::move(items))
    {
        popup = true;
        keep_inside = true;
        place_at({ 0, 0 });
    }

    void Menu::place_at(Point at, float min_width)
    {
        at_ = at;
        min_width_ = std::max(min_width, style.min_width);
        placement = top_left(at.x, at.y, min_width_, content_height());
    }

    float Menu::content_height() const
    {
        float h = style.padding.y0 + style.padding.y1;
        for (const MenuItem& item : items)
            h += item.separator ? style.separator_height : style.row_height;
        return h;
    }

    Rect Menu::row_rect(std::size_t index) const
    {
        float y = rect().y0 + style.padding.y0;
        for (std::size_t i = 0; i < index && i < items.size(); ++i)
            y += items[i].separator ? style.separator_height : style.row_height;
        const float h = index < items.size() && items[index].separator ? style.separator_height : style.row_height;
        return { rect().x0 + style.padding.x0, y, rect().x1 - style.padding.x1, y + h };
    }

    std::optional<std::size_t> Menu::row_at(Point p) const
    {
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            if (contains(row_rect(i), p))
                return i;
        }
        return std::nullopt;
    }

    bool Menu::choosable(std::size_t index) const
    {
        return index < items.size() && items[index].enabled && !items[index].separator;
    }

    void Menu::highlight(std::optional<std::size_t> index)
    {
        highlighted_ = index && choosable(*index) ? index : std::nullopt;
    }

    void Menu::move_highlight(int direction, std::size_t from)
    {
        const auto n = static_cast<std::ptrdiff_t>(items.size());
        for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(from); i >= 0 && i < n; i += direction)
        {
            if (choosable(static_cast<std::size_t>(i)))
            {
                highlighted_ = static_cast<std::size_t>(i);
                return;
            }
        }
    }

    void Menu::choose(std::size_t index)
    {
        if (!choosable(index))
            return;
        choice_ = index;
        close();
    }

    void Menu::on_hover(Point p)
    {
        highlight(row_at(p));
    }

    void Menu::on_drag(Point p)
    {
        highlight(row_at(p));
    }

    void Menu::on_release(Point p, bool inside)
    {
        if (!inside)
            return;
        if (const auto row = row_at(p))
            choose(*row);
    }

    bool Menu::on_key(const KeyPress& key)
    {
        const std::size_t last = items.empty() ? 0 : items.size() - 1;
        switch (key.key)
        {
            case Key::Up:
                if (highlighted_ && *highlighted_ > 0)
                    move_highlight(-1, *highlighted_ - 1);
                else if (!highlighted_)
                    move_highlight(-1, last);
                return true;
            case Key::Down:
                move_highlight(1, highlighted_ ? *highlighted_ + 1 : 0);
                return true;
            case Key::Home:
                move_highlight(1, 0);
                return true;
            case Key::End:
                move_highlight(-1, last);
                return true;
            case Key::Enter:
            case Key::Space:
                if (highlighted_)
                    choose(*highlighted_);
                return true;
            case Key::Escape:
                close();
                return true;
            case Key::Left:
            case Key::Right:
                sideways_ += key.key == Key::Right ? 1 : -1;
                return true;
            case Key::Character:
                return true; // Space arrives as one too; nothing else types here
            default:
                return false;
        }
    }

    void Menu::update(const UpdateContext& context)
    {
        const auto f = context.fonts ? context.fonts->get(style.font) : std::nullopt;
        float width = min_width_;
        if (f)
        {
            float text = 0;
            float shortcut = 0;
            for (const MenuItem& item : items)
            {
                text = std::max(text, text_width(*f, item.text));
                shortcut = std::max(shortcut, text_width(*f, item.shortcut));
            }
            const float inner = style.tick_room + text + (shortcut > 0 ? style.shortcut_gap + shortcut : 0) + style.text_end_room;
            width = std::max(width, std::ceil(inner + style.padding.x0 + style.padding.x1));
        }
        // Keep the top-left corner where it was put; layout keeps the menu on the desktop.
        const Rect& o = placement.offsets;
        placement = top_left(o.x0, o.y0, width, content_height());
    }

    void Menu::draw(DrawContext& context) const
    {
        if (!style.background.empty())
            draw_skin(context.list, context.skins.find(style.background), rect());
        const auto f = context.fonts.get(style.font);
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            const MenuItem& item = items[i];
            const Rect row = row_rect(i);
            if (item.separator)
            {
                const float y = std::round((row.y0 + row.y1) / 2);
                context.list.fill({ row.x0 + 2, y, row.x1 - 2, y + 1 }, style.separator_colour);
                continue;
            }
            if (highlighted_ == i)
            {
                if (const auto s = style.highlight_skin.empty() ? std::nullopt : context.skins.find(style.highlight_skin))
                    draw_skin(context.list, s, row);
                else
                    context.list.fill(row, style.highlight_colour);
            }
            if (item.checked)
            {
                const float size = std::min(style.tick_room - 4, row.height() - 4);
                const float x = row.x0 + std::round((style.tick_room - size) / 2);
                const float y = row.y0 + std::round((row.height() - size) / 2);
                const Rect box { x, y, x + size, y + size };
                if (const auto tick = style.tick.empty() ? std::nullopt : context.skins.find(style.tick))
                    draw_skin(context.list, tick, box);
                else
                    context.list.fill(box, style.text_colour);
            }
            if (!f)
                continue;
            const Color colour = item.enabled && enabled ? style.text_colour : style.disabled_text_colour;
            draw_text(context.list, *f, item.text, { row.x0 + style.tick_room, row.y0, row.x1, row.y1 }, { colour, Align::Start, Align::Centre, false });
            if (!item.shortcut.empty())
                draw_text(context.list, *f, item.shortcut, { row.x0, row.y0, row.x1 - style.text_end_room / 2, row.y1 }, { colour, Align::End, Align::Centre, false });
        }
    }

} // namespace rct3::ui
