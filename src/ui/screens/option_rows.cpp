// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/option_rows.hpp"

#include <cmath>
#include <format>
#include <utility>

namespace rct3::ui
{

    namespace
    {

        constexpr float kNameGap = 5; ///< Between a name and the control column

    } // namespace

    OptionRows::OptionRows(Widget& page, float width, OptionRowsStyle style, config::GameOptions& options, std::bitset<config::kGameOptionCount> inert)
        : page_(page)
        , width_(width)
        , style_(std::move(style))
        , options_(options)
        , inert_(inert)
    {
    }

    OptionRows::Row& OptionRows::add_row()
    {
        Row& row = rows_.emplace_back();
        row.gap_before = rows_.size() == 1 ? 0 : std::max(style_.row_gap, next_gap_);
        next_gap_ = 0;
        return row;
    }

    std::string OptionRows::control_name(std::string_view widget, std::string_view kind)
    {
        return widget.empty() ? std::string(kind) + std::to_string(count_++) : std::string(widget);
    }

    Label& OptionRows::name_label(Row& row, std::u16string name, float x, float width, std::u16string tip, std::string_view widget)
    {
        auto& label = page_.add<Label>(widget.empty() ? control_name({ }, "name") : std::format("{}_label", widget), std::move(name), style_.font);
        label.style.colour = style_.text_colour;
        label.style.vertical = Align::Centre;
        label.tooltip = std::move(tip);
        row.parts.push_back({ &label, x, width, 0 });
        return label;
    }

    Tickbox& OptionRows::tick(config::GameOption option, std::u16string name, std::u16string tip)
    {
        Row& row = add_row();
        const std::string_view widget = config::game_option_info(option).name;
        auto& box = page_.add<Tickbox>(control_name(widget, "tick"), style_.tick_box, style_.tick);
        box.tooltip = tip;
        row.parts.push_back({ &box, style_.left, style_.tick_size, style_.tick_size });
        const float text_x = style_.left + style_.tick_size + style_.tick_gap;
        name_label(row, std::move(name), text_x, width_ - text_x, std::move(tip), widget);
        bind(row, { option, Kind::Tick, &box });
        return box;
    }

    Dropdown& OptionRows::choice(config::GameOption option, std::u16string name, std::vector<std::u16string> items, std::u16string tip)
    {
        Dropdown& d = list(std::move(name), std::move(items), std::move(tip), std::string(config::game_option_info(option).name));
        Row& row = rows_.back();
        bind(row, { option, Kind::Choice, &d });
        return d;
    }

    Dropdown& OptionRows::list(std::u16string name, std::vector<std::u16string> items, std::u16string tip, std::string widget)
    {
        Row& row = add_row();
        name_label(row, std::move(name), style_.left, style_.control_x - style_.left - kNameGap, tip, widget);
        auto& d = page_.add<Dropdown>(control_name(widget, "list"), style_.dropdown);
        d.items = std::move(items);
        d.tooltip = std::move(tip);
        row.parts.push_back({ &d, style_.control_x, style_.control_width, 0 });
        return d;
    }

    void OptionRows::bind(Row& row, const Binding& binding)
    {
        row.bindings.push_back(binding);
        row.inert = row.inert || inert_.test(static_cast<std::size_t>(binding.option));
        set_control(binding);
    }

    float OptionRows::slider_height() const
    {
        return style_.slider_inset > 0 ? style_.row_height - 2 * style_.slider_inset : 0;
    }

    Slider& OptionRows::level(config::GameOption option, std::u16string name, std::u16string tip)
    {
        Row& row = add_row();
        const config::GameOptionInfo& info = config::game_option_info(option);
        name_label(row, std::move(name), style_.left, style_.control_x - style_.left - kNameGap, tip, info.name);
        auto& s = page_.add<Slider>(control_name(info.name, "level"), style_.slider);
        s.minimum = info.minimum;
        s.maximum = info.maximum;
        s.step = info.step;
        s.tooltip = std::move(tip);
        row.parts.push_back({ &s, style_.control_x, style_.control_width, slider_height() });
        bind(row, { option, Kind::Level, &s });
        return s;
    }

    Spinner& OptionRows::number(config::GameOption option, std::u16string name, std::u16string tip)
    {
        Row& row = add_row();
        const config::GameOptionInfo& info = config::game_option_info(option);
        name_label(row, std::move(name), style_.left, style_.control_x - style_.left - kNameGap, tip, info.name);
        auto& s = page_.add<Spinner>(control_name(info.name, "number"), style_.spinner);
        s.minimum = static_cast<int>(info.minimum);
        s.maximum = static_cast<int>(info.maximum);
        s.step = std::max(1, static_cast<int>(info.step));
        s.tooltip = std::move(tip);
        row.parts.push_back({ &s, style_.control_x, style_.number_width, 0 });
        bind(row, { option, Kind::Number, &s });
        return s;
    }

    void OptionRows::pair(config::DetailPair options, std::u16string name, std::u16string tip)
    {
        Row& row = add_row();
        name_label(row, std::move(name), style_.left, kFirstColumn - style_.left - kNameGap, std::move(tip), config::game_option_info(options.detail).name);
        for (const auto& [option, x] : { std::pair { options.detail, kFirstColumn }, std::pair { options.distance, kSecondColumn } })
        {
            const config::GameOptionInfo& info = config::game_option_info(option);
            auto& s = page_.add<Slider>(control_name(info.name, "level"), style_.slider);
            s.minimum = info.minimum;
            s.maximum = info.maximum;
            s.step = info.step;
            row.parts.push_back({ &s, x, kColumnWidth, slider_height() });
            bind(row, { option, Kind::Level, &s });
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

    Label& OptionRows::note(std::u16string text, int lines)
    {
        Row& row = add_row();
        row.height = static_cast<float>(std::max(1, lines)) * style_.row_height;
        Label& label = name_label(row, std::move(text), style_.left, width_ - style_.left, { });
        label.style.wrap = true;
        label.style.vertical = Align::Start;
        return label;
    }

    KeyBox& OptionRows::keys(std::u16string name, std::vector<KeyCombo> combos, std::u16string prompt, std::u16string tip)
    {
        Row& row = add_row();
        name_label(row, std::move(name), style_.left, style_.control_x - style_.left - kNameGap, tip);
        auto& box = page_.add<KeyBox>(control_name({ }, "keys"), std::move(combos));
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

    void OptionRows::disable()
    {
        if (!rows_.empty())
            rows_.back().inert = true;
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
            const bool usable = !locked && !row.inert && (!row.enabled || holds(*row.enabled));
            for (const Part& p : row.parts)
            {
                p.widget->visible = shown;
                p.widget->enabled = usable;
                if (auto* label = dynamic_cast<Label*>(p.widget))
                    label->style.colour = usable ? style_.text_colour : style_.disabled_colour;
            }
            if (!shown)
                continue;
            if (!first)
                y += row.gap_before;
            first = false;
            const float row_height = row.height > 0 ? row.height : style_.row_height;
            for (const Part& p : row.parts)
            {
                const float h = p.height > 0 ? p.height : row_height;
                p.widget->placement = top_left(p.x, y + std::round((row_height - h) / 2), p.width, h);
            }
            y += row_height;
        }
        height_ = y;
        return changed;
    }

} // namespace rct3::ui
