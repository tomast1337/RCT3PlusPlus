// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/dropdown.hpp"

#include "ui/text_layout.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::ui
{

    namespace
    {

        Widget& root_of(Widget& w)
        {
            Widget* r = &w;
            while (r->parent() != nullptr)
                r = r->parent();
            return *r;
        }

    } // namespace

    Dropdown::Dropdown(std::string name, DropdownStyle style)
        : Widget(std::move(name))
        , style(std::move(style))
    {
    }

    Dropdown::~Dropdown()
    {
        if (menu_ != nullptr)
            menu_->close();
    }

    void Dropdown::select(std::optional<std::size_t> index)
    {
        selected_ = index && *index < items.size() ? index : std::nullopt;
    }

    bool Dropdown::take_change()
    {
        if (changes_ == 0)
            return false;
        --changes_;
        return true;
    }

    void Dropdown::pick(std::size_t index)
    {
        if (index >= items.size())
            return;
        if (selected_ != index)
            ++changes_;
        selected_ = index;
    }

    void Dropdown::open()
    {
        Widget& root = root_of(*this);
        if (&root == this || items.empty())
            return;
        std::vector<MenuItem> rows;
        rows.reserve(items.size());
        for (std::size_t i = 0; i < items.size(); ++i)
            rows.push_back({ .text = items[i], .checked = selected_ == i });
        menu_ = &root.add<Menu>(std::string(name()) + ".menu", style.menu, std::move(rows));
        menu_->place_at({ rect().x0 - root.rect().x0, rect().y1 - root.rect().y0 }, rect().width());
        menu_->highlight(selected_);
        dragged_into_menu_ = false;
    }

    void Dropdown::on_press(Point)
    {
        // The press already closed an open menu (a popup closes when a press misses it): leave it shut.
        if (menu_ != nullptr)
            return;
        open();
    }

    void Dropdown::on_drag(Point p)
    {
        // The press that opened the menu still belongs to this widget: pass it on, so a drag down the menu
        // and a release on a row chooses.
        if (menu_ == nullptr || menu_->closing())
            return;
        if (contains(menu_->rect(), p))
        {
            dragged_into_menu_ = true;
            menu_->on_drag(p);
        }
    }

    void Dropdown::on_release(Point p, bool)
    {
        if (menu_ != nullptr && !menu_->closing() && dragged_into_menu_ && contains(menu_->rect(), p))
            menu_->on_release(p, true);
    }

    bool Dropdown::on_key(const KeyPress& key)
    {
        if (items.empty())
            return false;
        const std::size_t at = selected_.value_or(0);
        switch (key.key)
        {
            case Key::Up:
                pick(selected_ && at > 0 ? at - 1 : 0);
                return true;
            case Key::Down:
                pick(selected_ ? std::min(at + 1, items.size() - 1) : 0);
                return true;
            case Key::Home:
                pick(0);
                return true;
            case Key::End:
                pick(items.size() - 1);
                return true;
            case Key::Enter:
            case Key::Space:
                if (menu_ == nullptr)
                    open();
                return true;
            case Key::Character:
                return true; // Space arrives as one too
            default:
                return false;
        }
    }

    void Dropdown::update(const UpdateContext&)
    {
        if (menu_ == nullptr)
            return;
        if (const auto choice = menu_->take_choice())
            pick(*choice);
        (void)menu_->take_sideways();
        if (menu_->closing())
            menu_ = nullptr;
    }

    void Dropdown::draw(DrawContext& context) const
    {
        const Rect& r = rect();
        const std::string& back = has_keyboard_focus() && !style.focused.empty() ? style.focused : style.field;
        if (!back.empty())
            draw_skin(context.list, context.skins.find(back), r);

        const float size = std::min(style.opener_width, r.height());
        const float y = std::round(r.y0 + (r.height() - size) / 2);
        const Rect arrow { r.x1 - size - std::round((r.height() - size) / 2), y, r.x1 - std::round((r.height() - size) / 2), y + size };
        const SkinState state = !enabled ? SkinState::Disabled : menu_ != nullptr || pressed() ? SkinState::Pressed : hovered() ? SkinState::UpOver : SkinState::Up;
        draw_skin_state(context.list, context.skins, style.opener, state, arrow);

        const Font* f = context.fonts.get(style.font);
        if (f == nullptr || !selected_ || *selected_ >= items.size())
            return;
        const Rect text { r.x0 + style.text_indent, r.y0, arrow.x0 - 2, r.y1 };
        if (context.list.push_clip(text))
            draw_text(context.list, *f, items[*selected_], text, { enabled ? style.text_colour : style.disabled_text_colour, Align::Start, Align::Centre, false });
        context.list.pop_clip();
    }

} // namespace rct3::ui
