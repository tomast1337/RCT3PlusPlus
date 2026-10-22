// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/info_panel.hpp"

#include "core/maybe.hpp"
#include "ui/shape.hpp"
#include "ui/widgets/edit_box.hpp"
#include "ui/widgets/label.hpp"
#include "ui/widgets/panel.hpp"
#include "ui/widgets/tickbox.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>

namespace rct3::ui
{

    Point info_tab_offset(int slot)
    {
        const float degrees = info_panel::kFirstAngle + info_panel::kSlotAngle * static_cast<float>(slot);
        const float a = degrees * std::numbers::pi_v<float> / 180;
        return { std::round(info_panel::kRingRadius * std::cos(a)), std::round(-info_panel::kRingRadius * std::sin(a)) };
    }

    Point info_choice_offset(int slot, std::size_t i, std::size_t count)
    {
        const float degrees = info_panel::kFirstAngle + info_panel::kSlotAngle * static_cast<float>(slot);
        float a = degrees * std::numbers::pi_v<float> / 180;
        if (count == 2)
            a += i == 0 ? info_panel::kTwoChoiceSpread : -info_panel::kTwoChoiceSpread;
        else if (count == 3)
            a += info_panel::kThreeChoiceSpread * (1 - static_cast<float>(i));
        return { std::round(info_panel::kChoiceRadius * std::cos(a)), std::round(-info_panel::kChoiceRadius * std::sin(a)) };
    }

    InfoPanel::InfoPanel(std::string name, InfoPanelStyle style)
        : Widget(std::move(name))
        , style_(std::move(style))
    {
        // The panel spans the screen so its parts can sit by its corner; it lets the pointer through elsewhere.
        placement = fill();
        body_ = &add<Widget>("body");
        body_->clip_children = true;
        // Over the band along the strip's top, outside the body, which cuts its lines to itself.
        title_ = &add<Label>("title", std::u16string { }, FontRole::Medium);
        title_->style.colour = style_.text_colour;
        title_->style.vertical = Align::Centre;
        close_ = &add<Button>("close", std::string { }, style_.close_icon);
        const float close_bottom = info_panel::kCloseBottom + style_.raise;
        close_->placement = style_.left ? bottom_left(info_panel::kCloseRight, close_bottom, info_panel::kCloseSize, info_panel::kCloseSize) : bottom_right(info_panel::kCloseRight, close_bottom, info_panel::kCloseSize, info_panel::kCloseSize);
        visible = false;
        set_body_size(0, 0);
    }

    void InfoPanel::show(std::vector<InfoTab> tabs, std::size_t picked)
    {
        close_choices();
        close_palette();
        paints_.clear();
        paint_colours_.clear();
        for (Button* b : tabs_)
            (void)remove(*b);
        tabs_.clear();
        slots_.clear();
        tab_choices_.clear();
        actions_.clear();
        for (std::size_t i = 0; i < tabs.size(); ++i)
        {
            PaintButton* paint = nullptr;
            // Named by its icon, the key its maker gave it; a paint tab by what it is.
            const std::string tab_name = tabs[i].icon.empty() ? std::format("paint{}", i) : tabs[i].icon;
            if (!tabs[i].paints.empty())
            {
                PaintButtonStyle look = style_.paint;
                look.face = style_.tab_face;
                paint = &add<PaintButton>(tab_name, std::move(look), std::move(tabs[i].paints));
            }
            Button& b = paint != nullptr ? *paint : add<Button>(tab_name, style_.tab_face, std::move(tabs[i].icon));
            paints_.push_back(paint);
            paint_colours_.push_back(tabs[i].colours);
            b.tooltip = std::move(tabs[i].help);
            b.toggle = tabs[i].choices.empty() && !tabs[i].action && paint == nullptr;
            b.swell = (info_panel::kTabGrown - info_panel::kTabSize) / 2;
            b.icon_scale = info_panel::kTabSize / info_panel::kIconSize;
            tabs_.push_back(&b);
            slots_.push_back(tabs[i].slot);
            tab_choices_.push_back(std::move(tabs[i].choices));
            actions_.push_back(tabs[i].action);
        }
        picked_ = picked;
        mark_picked();
        visible = true;
        closed_ = false;
        place();
    }

    void InfoPanel::hide()
    {
        close_choices();
        close_palette();
        visible = false;
    }

    void InfoPanel::open_choices(std::size_t tab)
    {
        close_choices();
        const std::vector<InfoChoice>& list = tab_choices_[tab];
        for (std::size_t i = 0; i < list.size(); ++i)
        {
            Button& b = add<Button>(list[i].icon.empty() ? std::format("choice{}", i) : list[i].icon, std::string { }, list[i].icon);
            b.tooltip = list[i].help;
            b.enabled = list[i].enabled;
            b.icon_scale = info_panel::kChoiceIconScale;
            choices_.push_back(&b);
        }
        choosing_ = tab;
        place();
    }

    void InfoPanel::close_choices()
    {
        for (Button* b : choices_)
            (void)remove(*b);
        choices_.clear();
        choosing_.reset();
    }

    void InfoPanel::open_palette(std::size_t tab, std::size_t pot)
    {
        close_palette();
        PaintButton& pots = *paints_[tab];
        const std::size_t count = paint_colours_.at(tab) > 0 ? paint_colours_.at(tab) : style_.palette_count;
        palette_ = &add<ColourPalette>("palette", style_.palette, count, style_.palette_columns, pots.colours.at(pot));
        pots.held = pot;
        painting_ = tab;
        painting_pot_ = pot;
        place();
    }

    void InfoPanel::close_palette()
    {
        if (palette_ != nullptr)
            (void)remove(*palette_);
        palette_ = nullptr;
        if (painting_ && *painting_ < paints_.size() && paints_[*painting_] != nullptr)
            paints_[*painting_]->held.reset();
        if (painting_page_ && *painting_page_ < controls_.size())
            static_cast<PaintButton*>(controls_[*painting_page_].second)->held.reset();
        painting_.reset();
        painting_page_.reset();
    }

    void InfoPanel::open_page_palette(std::size_t control, std::size_t pot)
    {
        close_palette();
        close_choices();
        auto& pots = *static_cast<PaintButton*>(controls_[control].second);
        palette_ = &add<ColourPalette>("palette", style_.palette, style_.palette_count, style_.palette_columns, pots.colours.at(pot));
        pots.held = pot;
        painting_page_ = control;
        painting_pot_ = pot;
        place();
    }

    void InfoPanel::set_body_size(float width, float height)
    {
        body_width_ = width;
        body_height_ = height;
        place();
    }

    void InfoPanel::show_page(const InfoPage& page)
    {
        if (painting_page_)
            close_palette();
        controls_.clear();
        while (!body_->children().empty())
            (void)body_->remove(*body_->children().back());
        sections_.clear();
        section_list_ = nullptr;
        value_rows_.clear();
        control_rows_.clear();
        measured_ = false;
        float y = 0;
        const auto line = [&](std::string name, const std::u16string& text, float x, float width, FontRole font) -> Label& {
            Label& l = body_->add<Label>(std::move(name), text, font);
            l.placement = top_left(x, y, width, info_panel::kLineHeight);
            l.style.colour = style_.text_colour;
            return l;
        };
        title_->text = page.title;
        if (!page.heading.empty())
        {
            line("heading", page.heading, 0, info_panel::kPageWidth, FontRole::Small);
            y += info_panel::kLineHeight;
        }
        if (page.sections.size() > 1 && page.section_list)
        {
            Dropdown& d = body_->add<Dropdown>("sections", style_.dropdown);
            d.items = page.sections;
            d.select(page.section);
            d.placement = top_left(0, y, info_panel::kChoiceWidth, info_panel::kControlHeight);
            section_list_ = &d;
            y += info_panel::kControlHeight + 2;
        }
        else if (page.sections.size() > 1)
        {
            // Each as wide as its share of the letters, so long names keep their room.
            std::size_t letters = 0;
            for (const std::u16string& s : page.sections)
                letters += s.size() + 1;
            float x = 0;
            for (std::size_t i = 0; i < page.sections.size(); ++i)
            {
                const float width = std::floor(info_panel::kPageWidth * static_cast<float>(page.sections[i].size() + 1) / static_cast<float>(letters));
                // Plain words: the shown one stands out by its colour.
                Button& b = body_->add<Button>(std::format("section{}", i), std::string { });
                b.text = page.sections[i];
                b.font = FontRole::Tiny;
                b.text_colour = i == page.section ? style_.picked_section_colour : style_.section_colour;
                b.placement = top_left(x, y, width, info_panel::kSectionHeight);
                x += width;
                sections_.push_back(&b);
            }
            y += info_panel::kSectionHeight + 2;
        }
        const auto lines = [&] {
            bool right = false; // Whether the row has a half line on its left, waiting for one on its right
            for (std::size_t i = 0; i < page.lines.size(); ++i)
            {
                const InfoLine& l = page.lines[i];
                if (l.half)
                {
                    // The right column stops short of the page's end, where the ring's tabs reach over it.
                    const float width = (info_panel::kPageWidth - info_panel::kRingReach) / 2 - info_panel::kPairGap;
                    const float x = right ? info_panel::kPageWidth - info_panel::kRingReach - width : 0;
                    // The value at the column's right edge, so a long label keeps the room a short value leaves it.
                    line(keyed_name(l.name, "label", i), l.label, x, width, FontRole::Small);
                    line(keyed_name(l.name, "value", i), l.value, x, width, FontRole::Small).style.horizontal = Align::End;
                    if (right)
                        y += info_panel::kLineHeight;
                    right = !right;
                    continue;
                }
                if (right)
                {
                    y += info_panel::kLineHeight;
                    right = false;
                }
                // A line without a value is text across the whole page.
                const float label_width = l.value.empty() ? info_panel::kPageWidth : info_panel::kValueColumn;
                Label& label = line(keyed_name(l.name, "label", i), l.label, 0, label_width, FontRole::Small);
                // A bar goes under its value, so it is made first; place_page puts it where the value goes.
                Panel* bar = l.bar ? &body_->add<Panel>(keyed_name(l.name, "bar", i), std::string { }) : nullptr;
                Panel* filled = l.bar ? &body_->add<Panel>(keyed_name(l.name, "filled", i), std::string { }) : nullptr;
                if (bar != nullptr)
                {
                    bar->colour = style_.bar_back;
                    filled->colour = l.bar_colour;
                    bar->placement = top_left(0, y + 1, info_panel::kBarWidth, info_panel::kLineHeight - 2);
                    filled->placement = bar->placement;
                }
                Label& value = line(keyed_name(l.name, "value", i), l.value, info_panel::kValueColumn, info_panel::kPageWidth - info_panel::kValueColumn, FontRole::Small);
                if (!l.value.empty())
                    value_rows_.push_back({ &label, &value, bar, filled, std::clamp(l.bar.value_or(0), 0.0f, 1.0f) });
                y += info_panel::kLineHeight;
            }
            if (right)
                y += info_panel::kLineHeight;
            if (!page.note.empty())
            {
                line("note", page.note, 0, info_panel::kPageWidth, FontRole::Small);
                y += info_panel::kLineHeight;
            }
        };
        if (!page.controls_first)
            lines();
        if (!page.graph.empty())
        {
            LinePlot& plot = body_->add<LinePlot>("graph");
            const float left = page.graph_axis.empty() ? 0 : info_panel::kAxisWidth;
            const float plot_width = info_panel::kPageWidth - left - (page.graph_axis.empty() ? 0 : info_panel::kRingReach);
            const float plot_height = page.graph_axis.empty() ? info_panel::kPlotHeight : info_panel::kAxisPlotHeight;
            if (!page.graph_axis.empty())
                y += info_panel::kLineHeight / 2; // Room for the top number's upper half
            plot.placement = top_left(left, y, plot_width, plot_height);
            for (std::size_t i = 0; i < page.graph_axis.size(); ++i)
            {
                // Each number centred on its height up the plot.
                const float at = page.graph_axis.size() > 1 ? plot_height * static_cast<float>(i) / static_cast<float>(page.graph_axis.size() - 1) : 0;
                Label& n = line(std::format("axis{}", i), page.graph_axis[i], 0, left - info_panel::kPairGap, FontRole::Tiny);
                n.placement = top_left(0, y + at - info_panel::kLineHeight / 2, left - info_panel::kPairGap, info_panel::kLineHeight);
                n.style.horizontal = Align::End;
                n.style.vertical = Align::Centre;
            }
            const float width = std::floor(info_panel::kPageWidth / static_cast<float>(page.graph.size()));
            y += plot_height + 1;
            for (std::size_t i = 0; i < page.graph.size(); ++i)
            {
                const Color colour = series_colour(i);
                if (page.graph[i].shown)
                    plot.series.push_back({ page.graph[i].values, colour });
                if (!page.graph_axis.empty())
                    continue;
                Label& name = body_->add<Label>(keyed_name(page.graph[i].key, page.graph[i].key.empty() ? "series" : "", i), page.graph[i].name, FontRole::Small);
                name.placement = top_left(width * static_cast<float>(i), y, width, info_panel::kLineHeight);
                name.style.colour = colour;
            }
            if (page.graph_axis.empty())
                y += info_panel::kLineHeight;
            else
                y += info_panel::kLineHeight / 2; // Room for the lowest number's lower half
        }
        // The controls go in rows under the rest, down the page's left or, for those that ask, down its right half.
        std::array<float, 2> bottom { y, y };
        for (std::size_t i = 0; i < page.controls.size(); ++i)
        {
            const InfoControl& c = page.controls[i];
            const auto column = static_cast<std::size_t>(std::clamp(c.column, 0, 1));
            const bool big = c.kind == InfoControlKind::Paint || !c.icon.empty();
            const float height = big ? info_panel::kPaintSize : info_panel::kControlHeight;
            const bool joins = c.beside && !control_rows_.empty() && control_rows_.back().column == column;
            const float top = joins ? control_rows_.back().y : bottom[column];
            bottom[column] = std::max(joins ? bottom[column] : top, top + height + 1);
            Label& label = line(keyed_name(c.name, "label", i), c.label, 0, 0, FontRole::Small);
            label.style.vertical = Align::Centre;
            if (c.colour)
                label.style.colour = *c.colour;
            if (c.series)
                label.style.colour = series_colour(*c.series);
            const std::string name = keyed_name(c.name, c.name.empty() ? "control" : "", i);
            Widget* w = nullptr;
            switch (c.kind)
            {
                case InfoControlKind::Number:
                {
                    Spinner& n = body_->add<Spinner>(name, style_.spinner);
                    n.minimum = c.minimum;
                    n.maximum = c.maximum;
                    n.step = c.step;
                    n.decimals = c.decimals;
                    n.unit = c.text;
                    n.unit = c.text;
                    n.set_value(c.value);
                    w = &n;
                    break;
                }
                case InfoControlKind::Choice:
                {
                    Dropdown& d = body_->add<Dropdown>(name, style_.dropdown);
                    d.items = c.items;
                    d.select(c.value >= 0 ? std::optional<std::size_t>(static_cast<std::size_t>(c.value)) : std::nullopt);
                    w = &d;
                    break;
                }
                case InfoControlKind::Tick:
                {
                    Tickbox& t = body_->add<Tickbox>(name, style_.tick_box, style_.tick);
                    t.ticked = c.value != 0;
                    w = &t;
                    break;
                }
                case InfoControlKind::Button:
                {
                    Button& b = body_->add<Button>(name, c.icon.empty() ? style_.button : std::string { }, c.icon);
                    b.text = c.text;
                    b.font = FontRole::Small;
                    b.icon_scale = info_panel::kPaintSize / info_panel::kIconSize;
                    w = &b;
                    break;
                }
                case InfoControlKind::Text:
                {
                    EditBox& e = body_->add<EditBox>(name, c.text);
                    e.skin = style_.spinner.field;
                    e.focused_skin = style_.spinner.field_focused;
                    e.max_length = static_cast<std::size_t>(std::max(c.maximum, 0));
                    w = &e;
                    break;
                }
                case InfoControlKind::Paint:
                {
                    PaintButtonStyle look = style_.paint;
                    look.face = style_.tab_face;
                    w = &body_->add<PaintButton>(name, std::move(look), c.paints);
                    break;
                }
                case InfoControlKind::Words:
                    break;
            }
            if (w != nullptr)
            {
                w->enabled = c.enabled;
                w->tooltip = c.help;
            }
            if (!c.enabled)
                label.style.colour.a /= 2;
            controls_.emplace_back(c, w);
            control_rows_.push_back({ &label, column, top, height });
        }
        y = std::max(bottom[0], bottom[1]);
        if (page.controls_first)
            lines();
        page_width_ = page.width > 0 ? page.width : info_panel::kPageWidth;
        place_page(std::nullopt);
        // An empty page has no strip.
        set_body_size(y > 0 ? page_width_ : 0, y);
    }

    void InfoPanel::place_page(std::optional<const Font&> font)
    {
        const float gap = info_panel::kPairGap;
        // shortcut: without the fonts a label is guessed at 6 units a letter; they are measured on the next update.
        const auto needs = [&](const std::u16string& text) { return text.empty() ? 0.0f : (font ? std::ceil(text_width(*font, text)) : 6.0f * static_cast<float>(text.size())) + 2 * gap; };
        const bool halves = std::ranges::any_of(control_rows_, [](const ControlRow& r) { return r.column == 1; });
        // The ring's tabs reach over the page's right end.
        const float end = page_width_ - info_panel::kRingReach;
        const std::array<std::pair<float, float>, 2> spans { { { 0.0f, halves ? end / 2 - gap : end }, { end / 2 + gap, end } } };
        const auto starts_row = [](const InfoControl& c) { return !c.beside && !c.label.empty() && c.kind != InfoControlKind::Tick && c.kind != InfoControlKind::Words; };
        // The lines' values and the controls starting their rows after a label line up clear of the widest label
        // of their column; on a page of halves the left starts no further than its labels need.
        std::array<float, 2> aligned { halves ? 0.0f : info_panel::kValueColumn, 0.0f };
        for (const ValueRow& r : value_rows_)
            aligned[0] = std::max(aligned[0], needs(r.label->text));
        for (std::size_t i = 0; i < controls_.size(); ++i)
            if (starts_row(controls_[i].first))
                aligned[control_rows_[i].column] = std::max(aligned[control_rows_[i].column], needs(controls_[i].first.label));
        for (const ValueRow& r : value_rows_)
        {
            r.label->placement.offsets.x1 = std::max(r.label->placement.offsets.x1, aligned[0]);
            r.value->placement.offsets.x0 = aligned[0];
            r.value->placement.offsets.x1 = std::max(page_width_, aligned[0]);
            if (r.bar == nullptr)
                continue;
            // The value written on its bar.
            r.value->placement.offsets.x0 = aligned[0] + gap;
            r.bar->placement.offsets.x0 = aligned[0];
            r.bar->placement.offsets.x1 = aligned[0] + info_panel::kBarWidth;
            r.filled->placement.offsets.x0 = aligned[0];
            r.filled->placement.offsets.x1 = aligned[0] + std::round(info_panel::kBarWidth * r.fill);
        }
        float x = 0;
        for (std::size_t i = 0; i < controls_.size(); ++i)
        {
            const auto& [c, w] = controls_[i];
            const ControlRow& row = control_rows_[i];
            const auto [x0, x1] = spans[row.column];
            if (!c.beside || i == 0 || control_rows_[i - 1].column != row.column)
                x = x0;
            Label& label = *row.label;
            if (c.kind == InfoControlKind::Words)
            {
                label.placement = top_left(x, row.y, std::max(x1 - x, 0.0f), row.height);
                x = x1;
                continue;
            }
            float width = c.width;
            if (width <= 0)
            {
                switch (c.kind)
                {
                    case InfoControlKind::Number:
                        width = info_panel::kNumberWidth;
                        break;
                    case InfoControlKind::Tick:
                        width = info_panel::kTickSize;
                        break;
                    case InfoControlKind::Paint:
                        width = info_panel::kPaintSize * static_cast<float>(c.paints.size());
                        break;
                    case InfoControlKind::Choice:
                        width = info_panel::kChoiceWidth;
                        break;
                    case InfoControlKind::Button:
                    case InfoControlKind::Text:
                    case InfoControlKind::Words:
                        width = c.icon.empty() ? x1 : info_panel::kPaintSize;
                        break;
                }
            }
            const float w_height = c.kind == InfoControlKind::Tick ? info_panel::kTickSize : row.height;
            if (c.kind == InfoControlKind::Tick)
            {
                // The box first, its label after it.
                w->placement = top_left(x, row.y + (row.height - w_height) / 2, width, w_height);
                x += width + gap;
                label.placement = top_left(x, row.y, std::max(needs(c.label), x1 - x), row.height);
                x += needs(c.label);
                continue;
            }
            label.placement = top_left(x, row.y, needs(c.label), row.height);
            x = starts_row(c) ? std::max(x0 + aligned[row.column], x + needs(c.label)) : x + needs(c.label);
            // What would run past the column keeps to it.
            if (c.icon.empty() && c.kind != InfoControlKind::Number && c.kind != InfoControlKind::Paint)
                width = std::min(width, std::max(x1 - x, 0.0f));
            w->placement = top_left(x, row.y + (row.height - w_height) / 2, width, w_height);
            x += width + gap;
        }
    }

    std::optional<Widget&> InfoPanel::control(int id) const
    {
        const auto found = std::ranges::find_if(controls_, [&](const auto& c) { return c.first.id == id; });
        return found == controls_.end() ? std::nullopt : maybe(found->second);
    }

    void InfoPanel::read_controls()
    {
        for (std::size_t i = 0; i < controls_.size(); ++i)
        {
            auto& [c, w] = controls_[i];
            switch (c.kind)
            {
                case InfoControlKind::Number:
                    if (auto* n = static_cast<Spinner*>(w); n->take_change())
                        change_now_ = InfoChange { c.id, n->value(), 0, { } };
                    break;
                case InfoControlKind::Choice:
                    if (auto* d = static_cast<Dropdown*>(w); d->take_change() && d->selected())
                        change_now_ = InfoChange { c.id, static_cast<int>(*d->selected()), 0, { } };
                    break;
                case InfoControlKind::Tick:
                    if (auto* t = static_cast<Tickbox*>(w); t->take_change())
                        change_now_ = InfoChange { c.id, t->ticked ? 1 : 0, 0, { } };
                    break;
                case InfoControlKind::Button:
                    if (static_cast<Button*>(w)->take_click())
                        change_now_ = InfoChange { c.id, 0, 0, { } };
                    break;
                case InfoControlKind::Text:
                    if (auto* e = static_cast<EditBox*>(w); e->take_commit())
                        change_now_ = InfoChange { c.id, 0, 0, e->text };
                    break;
                case InfoControlKind::Words:
                    break;
                case InfoControlKind::Paint:
                    if (const auto pot = static_cast<PaintButton*>(w)->take_pot())
                    {
                        // Pressing the pot again puts the colours away.
                        if (painting_page_ == i)
                            close_palette();
                        else
                            open_page_palette(i, *pot);
                    }
                    break;
            }
        }
    }

    Rect InfoPanel::disc() const
    {
        const Rect r = rect();
        const float right = style_.left ? r.x0 + info_panel::kDiscMargin + info_panel::kDisc : r.x1 - info_panel::kDiscMargin;
        const float bottom = r.y1 - info_panel::kDiscMargin - style_.raise;
        return { right - info_panel::kDisc, bottom - info_panel::kDisc, right, bottom };
    }

    Rect InfoPanel::view() const
    {
        const Rect d = disc();
        const float inset = (info_panel::kDisc - info_panel::kView) / 2;
        return { d.x0 + inset, d.y0 + inset, d.x1 - inset, d.y1 - inset };
    }

    Rect InfoPanel::strip() const
    {
        const float width = std::max(body_width_ + info_panel::kStripExtra, info_panel::kStripNarrowest);
        // Always full height, so it stands behind the whole disc however short its content.
        const float height = info_panel::kStripHeight;
        // Level with the disc's top, so the band along its top runs on into the disc's.
        const Rect d = disc();
        if (style_.left)
            return { d.x1 - info_panel::kStripOverlap, d.y0, d.x1 - info_panel::kStripOverlap + width, d.y0 + height };
        const float right = d.x0 + info_panel::kStripOverlap;
        return { right - width, d.y0, right, d.y0 + height };
    }

    void InfoPanel::place()
    {
        const Rect r = rect();
        const Rect d = disc();
        const Point middle { (d.x0 + d.x1) / 2 - r.x0, (d.y0 + d.y1) / 2 - r.y0 };
        const auto tab_offset = [&](int slot) {
            const Point at = info_tab_offset(slot);
            return style_.left ? Point { -at.x, at.y } : at;
        };
        for (std::size_t i = 0; i < tabs_.size(); ++i)
        {
            const Point at = tab_offset(slots_[i]);
            tabs_[i]->placement = top_left(middle.x + at.x - info_panel::kTabSize / 2, middle.y + at.y - info_panel::kTabSize / 2, info_panel::kTabSize, info_panel::kTabSize);
        }
        if (choosing_)
        {
            const Point tab = tab_offset(slots_[*choosing_]);
            for (std::size_t i = 0; i < choices_.size(); ++i)
            {
                Point at = info_choice_offset(slots_[*choosing_], i, choices_.size());
                if (style_.left)
                    at.x = -at.x;
                choices_[i]->placement = top_left(middle.x + tab.x + at.x - info_panel::kChoiceSize / 2, middle.y + tab.y + at.y - info_panel::kChoiceSize / 2, info_panel::kChoiceSize, info_panel::kChoiceSize);
            }
        }
        if (painting_ && palette_ != nullptr)
        {
            // Above its tab, as near the tab's middle as the screen allows.
            const Point size = ColourPalette::size(style_.palette, palette_->count(), style_.palette_columns);
            const Point tab = tab_offset(slots_[*painting_]);
            const float m = info_panel::kScreenMargin;
            const float x = std::clamp(middle.x + tab.x - size.x / 2, m, std::max(m, r.width() - m - size.x));
            const float y = std::clamp(middle.y + tab.y - info_panel::kTabSize / 2 - info_panel::kPaletteGap - size.y, m, std::max(m, r.height() - m - size.y));
            palette_->placement = top_left(std::round(x), std::round(y), size.x, size.y);
        }
        if (painting_page_ && palette_ != nullptr)
        {
            // Above the strip, over its pots.
            const Point size = ColourPalette::size(style_.palette, palette_->count(), style_.palette_columns);
            const Rect pots = controls_[*painting_page_].second->rect();
            const float m = info_panel::kScreenMargin;
            const float x = std::clamp((pots.x0 + pots.x1) / 2 - r.x0 - size.x / 2, m, std::max(m, r.width() - m - size.x));
            const float y = std::clamp(strip().y0 - r.y0 - info_panel::kPaletteGap - size.y, m, std::max(m, r.height() - m - size.y));
            palette_->placement = top_left(std::round(x), std::round(y), size.x, size.y);
        }
        const Rect s = strip();
        body_->placement = top_left(s.x0 - r.x0 + info_panel::kBodyLeft, s.y0 - r.y0 + info_panel::kBodyTop, body_width_, std::min(body_height_, info_panel::kStripHeight - info_panel::kBodyTop - info_panel::kBodyBottom));
        title_->placement = top_left(s.x0 - r.x0 + info_panel::kBodyLeft, s.y0 - r.y0, body_width_, std::min(style_.coded.band_height, info_panel::kBodyTop));
        title_->visible = body_width_ > 0;
    }

    void InfoPanel::mark_picked()
    {
        for (std::size_t i = 0; i < tabs_.size(); ++i)
            tabs_[i]->on = i == picked_;
    }

    bool InfoPanel::covers(Point p) const
    {
        const Rect d = disc();
        const float dx = p.x - (d.x0 + d.x1) / 2;
        const float dy = p.y - (d.y0 + d.y1) / 2;
        const float radius = info_panel::kDisc / 2;
        return dx * dx + dy * dy <= radius * radius || ((!style_.left || body_width_ > 0) && contains(strip(), p));
    }

    void InfoPanel::update(const UpdateContext& context)
    {
        // The parts follow the screen's corner.
        place();
        if (const auto f = context.fonts && !measured_ ? context.fonts->get(FontRole::Small) : std::nullopt)
        {
            place_page(f);
            measured_ = true;
        }
        if (close_->take_click())
        {
            hide();
            closed_ = true;
            return;
        }
        for (std::size_t i = 0; i < choices_.size(); ++i)
        {
            if (choices_[i]->take_click() && choosing_)
            {
                choice_now_ = { *choosing_, i };
                close_choices();
                break;
            }
        }
        if (palette_ != nullptr && painting_page_)
        {
            if (const auto colour = palette_->take_pick())
            {
                auto& pots = *static_cast<PaintButton*>(controls_[*painting_page_].second);
                pots.colours.at(painting_pot_) = *colour;
                change_now_ = InfoChange { controls_[*painting_page_].first.id, static_cast<int>(*colour), painting_pot_, { } };
                close_palette();
            }
        }
        read_controls();
        if (palette_ != nullptr && painting_)
        {
            if (const auto colour = palette_->take_pick())
            {
                paint_now_ = { *painting_, painting_pot_, *colour };
                paints_[*painting_]->colours.at(painting_pot_) = *colour;
                close_palette();
            }
        }
        for (std::size_t i = 0; i < tabs_.size(); ++i)
        {
            if (paints_[i] != nullptr)
            {
                if (const auto pot = paints_[i]->take_pot())
                {
                    close_choices();
                    // Pressing it again puts the colours away.
                    if (painting_ == i)
                        close_palette();
                    else
                        open_palette(i, *pot);
                }
                continue;
            }
            if (!tabs_[i]->take_click())
                continue;
            close_palette();
            if (!tab_choices_[i].empty())
            {
                if (choosing_ == i)
                    close_choices();
                else
                    open_choices(i);
                continue;
            }
            close_choices();
            if (actions_[i])
                action_now_ = i;
            else if (i != picked_)
            {
                picked_ = i;
                picked_now_ = i;
            }
        }
        for (std::size_t i = 0; i < sections_.size(); ++i)
        {
            if (sections_[i]->take_click())
                section_now_ = i;
        }
        if (section_list_ != nullptr && section_list_->take_change() && section_list_->selected())
            section_now_ = *section_list_->selected();
        mark_picked();
    }

    void InfoPanel::draw(DrawContext& context) const
    {
        if (style_.look == PanelLook::Coded || style_.left)
            draw_coded(context.list);
        else
            draw_skins(context);
    }

    void InfoPanel::draw_coded(DrawList& list) const
    {
        const CodedInfoPanel& c = style_.coded;
        const auto shifted = [](Rect r, float by) { return Rect { r.x0 + by, r.y0 + by, r.x1 + by, r.y1 + by }; };
        const auto shape = [](Rect r, Corners corners, Color top, Color bottom) {
            Shape out;
            out.rect = r;
            out.corners = corners;
            out.top = top;
            out.bottom = bottom;
            return out;
        };
        const auto plain = [&](Rect r, Corners corners, Color colour) { list.shape(shape(r, corners, colour, colour)); };

        if (body_width_ > 0)
        {
            const Rect s = strip();
            const Rect slab { s.x0, s.y0, s.x1 - c.strip_shadow, s.y1 - c.strip_shadow };
            const Corners top { c.corner, c.corner, 0, 0 };
            plain(shifted(slab, info_panel::kShadowOffset), top, c.shadow);
            plain(slab, top, c.body);
            plain({ slab.x0, slab.y0, slab.x1, slab.y0 + c.band_height }, top, c.band);
        }

        // The disc: flat where it meets the strip, round on the right.
        const Rect d = disc();
        const float half = d.width() / 2;
        // Turned the other way, round on the left and flat on the right only while the strip shows.
        const Corners flat = style_.left ? (body_width_ > 0 ? Corners { half, 0, 0, half } : round_corners(half)) : Corners { 0, half, half, 0 };
        plain(shifted(d, info_panel::kShadowOffset), flat, c.shadow);
        plain(shifted(d, info_panel::kShadowOffset), round_corners(half), c.shadow);
        plain(d, flat, c.disc);
        if (!style_.left)
            plain({ d.x0, d.y0, d.x0 + half, d.y0 + c.band_height }, { }, c.band);
        else if (body_width_ > 0)
            plain({ d.x0 + half, d.y0, d.x1, d.y0 + c.band_height }, { }, c.band);
        Shape rim = shape(d, round_corners(half), c.disc, c.disc);
        rim.ring = c.rim;
        list.shape(rim);
        const Rect back { d.x0 + c.rim, d.y0 + c.rim, d.x1 - c.rim, d.y1 - c.rim };
        plain(back, round_corners(back.width() / 2), c.view_back);
        if (view_ != 0)
            list.disc_image(view(), view_, { 0, 0, 1, 1 });
    }

    void InfoPanel::draw_skins(DrawContext& context) const
    {
        const auto skin = [&](const std::string& name) { return name.empty() ? std::nullopt : context.skins.find(name); };
        if (body_width_ > 0)
            draw_skin(context.list, skin(style_.strip), strip());
        const Rect d = disc();
        const Rect shade { d.x0 + info_panel::kShadowOffset, d.y0 + info_panel::kShadowOffset, d.x1 + info_panel::kShadowOffset, d.y1 + info_panel::kShadowOffset };
        draw_skin(context.list, skin(style_.shadow), shade);
        draw_skin(context.list, skin(style_.rim_shadow), shade);
        draw_skin(context.list, skin(style_.disc), d);
        if (view_ != 0)
            context.list.disc_image(view(), view_, { 0, 0, 1, 1 });
        draw_skin(context.list, skin(style_.glass), d, { 255, 255, 255, static_cast<std::uint8_t>(std::lround(style_.glass_alpha * 255)) });
        draw_skin(context.list, skin(style_.rim), d);
    }

} // namespace rct3::ui
