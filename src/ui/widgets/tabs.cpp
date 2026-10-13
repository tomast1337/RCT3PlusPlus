// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/tabs.hpp"

#include "ui/text_layout.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::ui
{

    bool TabButton::on_key(const KeyPress& key)
    {
        if (!key.control && (key.key == Key::Left || key.key == Key::Right))
        {
            turn_ += key.key == Key::Right ? 1 : -1;
            return true;
        }
        return Button::on_key(key);
    }

    Tabs::Tabs(std::string name, TabsStyle style)
        : Widget(std::move(name))
        , style_(std::move(style))
    {
        bar_ = &add<Widget>("tabs");
        bar_->placement = { { 0, 0, 1, 0 }, { 0, 0, 0, style_.bar_height } };
    }

    Widget& Tabs::add_page(std::string name, std::u16string label)
    {
        TabButton& tab = bar_->add<TabButton>(name, style_.tab);
        tab.text = std::move(label);
        tab.text_inset = style_.text_inset;
        tab.font = style_.font;
        tab.toggle = true;
        tab.placement = top_left(static_cast<float>(tabs_.size()) * (style_.min_tab_width + style_.gap), 0, style_.min_tab_width, style_.bar_height);
        tabs_.push_back(&tab);

        Widget& page = add<Widget>(std::move(name));
        page.placement = { { 0, 0, 1, 1 }, { 0, style_.bar_height + style_.page_gap, 0, 0 } };
        pages_.push_back(&page);
        show(current_);
        return page;
    }

    void Tabs::show(std::size_t index)
    {
        if (index >= pages_.size())
            return;
        current_ = index;
        for (std::size_t i = 0; i < pages_.size(); ++i)
        {
            pages_[i]->visible = i == current_;
            tabs_[i]->on = i == current_;
        }
    }

    bool Tabs::take_change()
    {
        if (changes_ == 0)
            return false;
        --changes_;
        return true;
    }

    void Tabs::turn(int by)
    {
        if (pages_.empty())
            return;
        const auto n = static_cast<std::ptrdiff_t>(pages_.size());
        const auto next = ((static_cast<std::ptrdiff_t>(current_) + by) % n + n) % n;
        if (static_cast<std::size_t>(next) != current_)
            ++changes_;
        show(static_cast<std::size_t>(next));
    }

    bool Tabs::on_key(const KeyPress& key)
    {
        if (key.control && key.key == Key::Tab)
        {
            turn(key.shift ? -1 : 1);
            return true;
        }
        return false;
    }

    void Tabs::update(const UpdateContext& context)
    {
        for (std::size_t i = 0; i < tabs_.size(); ++i)
        {
            if (tabs_[i]->take_click() && i != current_)
            {
                ++changes_;
                current_ = i;
            }
        }
        for (TabButton* t : tabs_)
        {
            if (const int by = t->take_turn(); by != 0)
                turn(by);
        }
        show(current_); // Also puts back a click on the current tab, which toggled it off

        // Each tab as wide as its label, once there are fonts to measure it with.
        const Font* f = context.fonts != nullptr ? context.fonts->get(style_.font) : nullptr;
        float x = 0;
        for (TabButton* t : tabs_)
        {
            const float text = f != nullptr ? std::ceil(text_width(*f, t->text)) : 0;
            const float w = std::max(style_.min_tab_width, text + style_.text_room + style_.text_inset.x0 + style_.text_inset.x1);
            t->placement = top_left(x, 0, w, style_.bar_height);
            x += w + style_.gap;
        }
    }

} // namespace rct3::ui
