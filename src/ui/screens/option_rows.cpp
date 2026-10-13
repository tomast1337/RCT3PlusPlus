// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/option_rows.hpp"

#include <cmath>
#include <utility>

namespace rct3::ui
{

    namespace
    {

        constexpr float kNameGap = 5; ///< Between a name and the control column

    } // namespace

    OptionRows::OptionRows(Widget& page, float width, OptionRowsStyle style, config::GameOptions& options)
        : page_(page)
        , width_(width)
        , style_(std::move(style))
        , options_(options)
    {
    }

    OptionRows::Row& OptionRows::add_row()
    {
        Row& row = rows_.emplace_back();
        row.gap_before = rows_.size() == 1 ? 0 : std::max(style_.row_gap, next_gap_);
        next_gap_ = 0;
        return row;
    }

    Label& OptionRows::name_label(Row& row, std::u16string name, float x, float width, std::u16string tip)
    {
        auto& label = page_.add<Label>("name" + std::to_string(count_++), std::move(name), style_.font);
        label.style.colour = style_.text_colour;
        label.style.vertical = Align::Centre;
        label.tooltip = std::move(tip);
        row.parts.push_back({ &label, x, width, 0 });
        return label;
    }

    Tickbox& OptionRows::tick(config::GameOption option, std::u16string name, std::u16string tip)
    {
        Row& row = add_row();
        auto& box = page_.add<Tickbox>("tick" + std::to_string(count_++), style_.tick_box, style_.tick);
        box.tooltip = tip;
        row.parts.push_back({ &box, style_.left, style_.tick_size, style_.tick_size });
        const float text_x = style_.left + style_.tick_size + style_.tick_gap;
        name_label(row, std::move(name), text_x, width_ - text_x, std::move(tip));
        row.bindings.push_back({ option, Kind::Tick, &box });
        set_control(row.bindings.back());
        return box;
    }

    Dropdown& OptionRows::choice(config::GameOption option, std::u16string name, std::vector<std::u16string> items, std::u16string tip)
    {
        Dropdown& d = list(std::move(name), std::move(items), std::move(tip));
        Row& row = rows_.back();
        row.bindings.push_back({ option, Kind::Choice, &d });
        set_control(row.bindings.back());
        return d;
    }

    Dropdown& OptionRows::list(std::u16string name, std::vector<std::u16string> items, std::u16string tip)
    {
        Row& row = add_row();
        name_label(row, std::move(name), style_.left, style_.control_x - style_.left - kNameGap, tip);
        auto& d = page_.add<Dropdown>("list" + std::to_string(count_++), style_.dropdown);
        d.items = std::move(items);
        d.tooltip = std::move(tip);
        row.parts.push_back({ &d, style_.control_x, style_.control_width, 0 });
        return d;
    }

    float OptionRows::slider_height() const
    {
        return style_.slider_inset > 0 ? style_.row_height - 2 * style_.slider_inset : 0;
    }

    Slider& OptionRows::level(config::GameOption option, std::u16string name, std::u16string tip)
    {
        Row& row = add_row();
        name_label(row, std::move(name), style_.left, style_.control_x - style_.left - kNameGap, tip);
        auto& s = page_.add<Slider>("level" + std::to_string(count_++), style_.slider);
        const config::GameOptionInfo& info = config::game_option_info(option);
        s.minimum = info.minimum;
        s.maximum = info.maximum;
        s.step = info.step;
        s.tooltip = std::move(tip);
        row.parts.push_back({ &s, style_.control_x, style_.control_width, slider_height() });
        row.bindings.push_back({ option, Kind::Level, &s });
        set_control(row.bindings.back());
        return s;
    }

    Spinner& OptionRows::number(config::GameOption option, std::u16string name, std::u16string tip)
    {
        Row& row = add_row();
        name_label(row, std::move(name), style_.left, style_.control_x - style_.left - kNameGap, tip);
        auto& s = page_.add<Spinner>("number" + std::to_string(count_++), style_.spinner);
        const config::GameOptionInfo& info = config::game_option_info(option);
        s.minimum = static_cast<int>(info.minimum);
        s.maximum = static_cast<int>(info.maximum);
        s.step = std::max(1, static_cast<int>(info.step));
        s.tooltip = std::move(tip);
        row.parts.push_back({ &s, style_.control_x, style_.number_width, 0 });
        row.bindings.push_back({ option, Kind::Number, &s });
        set_control(row.bindings.back());
        return s;
    }

    void OptionRows::pair(config::DetailPair options, std::u16string name, std::u16string tip)
    {
        Row& row = add_row();
        name_label(row, std::move(name), style_.left, kFirstColumn - style_.left - kNameGap, std::move(tip));
        for (const auto& [option, x] : { std::pair { options.detail, kFirstColumn }, std::pair { options.distance, kSecondColumn } })
        {
            auto& s = page_.add<Slider>("level" + std::to_string(count_++), style_.slider);
            const config::GameOptionInfo& info = config::game_option_info(option);
            s.minimum = info.minimum;
            s.maximum = info.maximum;
            s.step = info.step;
            row.parts.push_back({ &s, x, kColumnWidth, slider_height() });
            row.bindings.push_back({ option, Kind::Level, &s });
            set_control(row.bindings.back());
        }
    }

    void OptionRows::headings(std::u16string first, std::u16string second)
    {
        Row& row = add_row();
        name_label(row, std::move(first), kFirstColumn, kSecondColumn - kFirstColumn, { });
        name_label(row, std::move(second), kSecondColumn, width_ - kSecondColumn, { });
    }

    Label& OptionRows::text(std::u16string name, std::u16string value, std::u16string tip)
    {
        Row& row = add_row();
        name_label(row, std::move(name), style_.left, style_.control_x - style_.left - kNameGap, tip);
        return name_label(row, std::move(value), style_.control_x, width_ - style_.control_x, std::move(tip));
    }

    KeyBox& OptionRows::keys(std::u16string name, std::vector<KeyCombo> combos, std::u16string prompt, std::u16string tip)
    {
        Row& row = add_row();
        name_label(row, std::move(name), style_.left, style_.control_x - style_.left - kNameGap, tip);
        auto& box = page_.add<KeyBox>("keys" + std::to_string(count_++), std::move(combos));
        box.skin = style_.key_box;
        box.focused_skin = style_.key_box_focused;
        box.font = style_.font;
        box.text_colour = style_.text_colour;
        box.prompt = std::move(prompt);
        box.tooltip = std::move(tip);
        // To the right edge: a command can have two combinations.
        row.parts.push_back({ &box, style_.control_x, width_ - style_.control_x - style_.left, 0 });
        return box;
    }

    void OptionRows::section()
    {
        next_gap_ = style_.section_gap;
    }

    void OptionRows::indent()
    {
        if (rows_.empty())
            return;
        for (Part& p : rows_.back().parts)
        {
            if (p.x < style_.control_x)
            {
                p.x += style_.indent;
                if (p.height == 0)
                    p.width -= style_.indent;
            }
        }
    }

    void OptionRows::shown_when(OptionIs condition)
    {
        if (!rows_.empty())
            rows_.back().shown = condition;
    }

    void OptionRows::enabled_when(OptionIs condition)
    {
        if (!rows_.empty())
            rows_.back().enabled = condition;
    }

    bool OptionRows::holds(const OptionIs& c) const
    {
        return options_.get(c.option) == c.value;
    }

    void OptionRows::set_control(const Binding& b)
    {
        const float value = options_.get(b.option);
        switch (b.kind)
        {
            case Kind::Tick:
                static_cast<Tickbox*>(b.control)->ticked = value != 0;
                break;
            case Kind::Choice:
                static_cast<Dropdown*>(b.control)->select(static_cast<std::size_t>(std::max(0.0f, value)));
                break;
            case Kind::Level:
                static_cast<Slider*>(b.control)->set_value(value);
                break;
            case Kind::Number:
                static_cast<Spinner*>(b.control)->set_value(static_cast<int>(std::lround(value)));
                break;
        }
    }

    void OptionRows::refresh()
    {
        for (const Row& row : rows_)
        {
            for (const Binding& b : row.bindings)
                set_control(b);
        }
    }

    bool OptionRows::update()
    {
        bool changed = false;
        for (const Row& row : rows_)
        {
            for (const Binding& b : row.bindings)
            {
                switch (b.kind)
                {
                    case Kind::Tick:
                        if (auto* t = static_cast<Tickbox*>(b.control); t->take_change())
                        {
                            options_.set(b.option, t->ticked ? 1.0f : 0.0f);
                            changed = true;
                        }
                        break;
                    case Kind::Choice:
                        if (auto* d = static_cast<Dropdown*>(b.control); d->take_change() && d->selected())
                        {
                            options_.set(b.option, static_cast<float>(*d->selected()));
                            changed = true;
                        }
                        break;
                    case Kind::Level:
                        if (auto* s = static_cast<Slider*>(b.control); s->take_change())
                        {
                            options_.set(b.option, s->value());
                            changed = true;
                        }
                        break;
                    case Kind::Number:
                        if (auto* s = static_cast<Spinner*>(b.control); s->take_change())
                        {
                            options_.set(b.option, static_cast<float>(s->value()));
                            changed = true;
                        }
                        break;
                }
            }
        }

        // Then place what shows, closing up behind hidden rows.
        float y = 0;
        bool first = true;
        for (const Row& row : rows_)
        {
            const bool shown = !row.shown || holds(*row.shown);
            const bool usable = !row.enabled || holds(*row.enabled);
            for (const Part& p : row.parts)
            {
                p.widget->visible = shown;
                p.widget->enabled = usable;
            }
            if (!shown)
                continue;
            if (!first)
                y += row.gap_before;
            first = false;
            for (const Part& p : row.parts)
            {
                const float h = p.height > 0 ? p.height : style_.row_height;
                p.widget->placement = top_left(p.x, y + std::round((style_.row_height - h) / 2), p.width, h);
            }
            y += style_.row_height;
        }
        height_ = y;
        return changed;
    }

} // namespace rct3::ui
