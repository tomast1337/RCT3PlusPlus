// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/menu_bar.hpp"

#include "ui/text_layout.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace rct3::ui
{

    namespace
    {

        constexpr float kUnmeasuredWidth = 60; ///< A title's width before there are fonts

        Widget& root_of(Widget& w)
        {
            Widget* r = &w;
            while (r->parent())
                r = r->parent();
            return *r;
        }

    } // namespace

    MenuBar::MenuBar(std::string name, MenuBarStyle style)
        : Widget(std::move(name))
        , style(std::move(style))
    {
    }

    MenuBar::~MenuBar()
    {
        close_menu();
    }

    Rect MenuBar::title_rect(std::size_t index) const
    {
        float x = rect().x0 + style.first_indent;
        for (std::size_t i = 0; i < index && i < menus.size(); ++i)
            x += i < widths_.size() ? widths_[i] : kUnmeasuredWidth;
        const float w = index < widths_.size() ? widths_[index] : kUnmeasuredWidth;
        return { x, rect().y0, x + w, rect().y1 };
    }

    std::optional<std::size_t> MenuBar::title_at(Point p) const
    {
        for (std::size_t i = 0; i < menus.size(); ++i)
        {
            if (contains(title_rect(i), p))
                return i;
        }
        return std::nullopt;
    }

    void MenuBar::close_menu()
    {
        if (menu_ != nullptr)
            menu_->close();
    }

    void MenuBar::open(std::size_t index)
    {
        Widget& root = root_of(*this);
        if (&root == this || index >= menus.size())
            return;
        if (menu_ != nullptr)
            menu_->close();
        const Rect title = title_rect(index);
        menu_ = &root.add<Menu>(std::format("{}.menu{}", name(), index), style.menu, menus[index].items);
        menu_->place_at({ title.x0 - root.rect().x0, title.y1 - root.rect().y0 });
        open_ = index;
        marked_ = index;
    }

    void MenuBar::on_press(Point p)
    {
        const auto title = title_at(p);
        // A press on the open menu's title already closed it (it missed the popup): leave it shut.
        if (!title || (menu_ != nullptr && open_ == *title))
            return;
        open(*title);
    }

    void MenuBar::on_hover(Point p)
    {
        // While a menu is open, the pointer moves it between titles.
        if (menu_ == nullptr || menu_->closing())
            return;
        if (const auto title = title_at(p); title && *title != open_)
            open(*title);
    }

    bool MenuBar::on_key(const KeyPress& key)
    {
        if (menus.empty())
            return false;
        switch (key.key)
        {
            case Key::Left:
                marked_ = marked_ == 0 ? menus.size() - 1 : marked_ - 1;
                return true;
            case Key::Right:
                marked_ = (marked_ + 1) % menus.size();
                return true;
            case Key::Enter:
            case Key::Space:
            case Key::Down:
                open(marked_);
                if (menu_ != nullptr)
                    menu_->highlight(std::nullopt);
                return true;
            case Key::Character:
                return true;
            default:
                return false;
        }
    }

    void MenuBar::update(const UpdateContext& context)
    {
        if (menu_ != nullptr)
        {
            if (const auto item = menu_->take_choice())
                choice_ = MenuChoice { open_, *item };
            if (const int by = menu_->take_sideways(); by != 0 && !menus.empty())
            {
                const auto n = static_cast<std::ptrdiff_t>(menus.size());
                const auto next = ((static_cast<std::ptrdiff_t>(open_) + by) % n + n) % n;
                open(static_cast<std::size_t>(next));
                menu_->highlight(std::nullopt);
            }
            else if (menu_->closing())
            {
                menu_ = nullptr;
            }
        }

        const auto f = context.fonts ? context.fonts->get(style.font) : std::nullopt;
        if (!f)
            return;
        widths_.clear();
        for (const MenuBarEntry& m : menus)
            widths_.push_back(std::ceil(text_width(*f, m.title) + 2 * style.title_padding));
    }

    void MenuBar::draw(DrawContext& context) const
    {
        if (!style.background.empty())
            draw_skin(context.list, context.skins.find(style.background), rect());
        const auto f = context.fonts.get(style.font);
        for (std::size_t i = 0; i < menus.size(); ++i)
        {
            const Rect title = title_rect(i);
            const bool lit = (menu_ != nullptr && open_ == i) || (has_keyboard_focus() && menu_ == nullptr && marked_ == i);
            if (lit)
            {
                if (const auto s = style.highlight_skin.empty() ? std::nullopt : context.skins.find(style.highlight_skin))
                    draw_skin(context.list, s, title);
                else
                    context.list.fill(title, style.highlight_colour);
            }
            if (f)
                draw_text(context.list, *f, menus[i].title, title, { style.text_colour, Align::Centre, Align::Centre, false });
        }
    }

} // namespace rct3::ui
