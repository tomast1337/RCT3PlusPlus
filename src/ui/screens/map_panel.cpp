// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/map_panel.hpp"

#include "ui/shape.hpp"

#include <utility>

namespace rct3::ui
{

    /// A legend row: a swatch and a name; a click shows or hides its layer.
    class MapLegendRow : public Widget
    {
    public:
        MapLegendRow(std::string name, const MapPanelStyle& style, Color colour, bool shown, bool round)
            : Widget(std::move(name))
            , shown(shown)
            , style_(style)
            , colour_(colour)
            , round_(round)
        {
        }

        bool shown;
        int changes = 0;

        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        [[nodiscard]] bool clicks() const override
        {
            return true;
        }
        void on_release(Point, bool inside) override
        {
            if (!inside)
                return;
            shown = !shown;
            ++changes;
        }

        void draw(DrawContext& context) const override
        {
            const Rect r = rect();
            if (hovered())
            {
                Shape back;
                back.rect = r;
                back.corners = round_corners(r.height() / 2);
                back.top = style_.hover;
                back.bottom = style_.hover;
                context.list.shape(back);
            }
            // The swatch: filled when the layer shows, only its outline when it does not.
            const float s = style_.swatch;
            const float top = r.y0 + (r.height() - s) / 2;
            Shape swatch;
            swatch.rect = { r.x0 + style_.swatch_gap / 2, top, r.x0 + style_.swatch_gap / 2 + s, top + s };
            swatch.corners = round_corners(round_ ? s / 2 : s / 4);
            const Color fill = shown ? Color { colour_.r, colour_.g, colour_.b, 255 } : Color { 0, 0, 0, 0 };
            swatch.top = fill;
            swatch.bottom = fill;
            swatch.outline = shown ? style_.swatch_line : Color { colour_.r, colour_.g, colour_.b, 255 };
            swatch.outline_width = shown ? 1.0f : 2.0f;
            context.list.shape(swatch);
        }

    private:
        const MapPanelStyle& style_;
        Color colour_;
        bool round_;
    };

    std::vector<float> legend_tops(const MapPanelStyle& style, const std::vector<MapLegendEntry>& entries)
    {
        std::vector<float> tops;
        float y = 0;
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            if (i > 0 && entries[i].starts_group)
                y += style.group_gap;
            tops.push_back(y);
            y += style.row_height;
        }
        return tops;
    }

    MapPanel::MapPanel(std::string name, const MapPanelStyle& style, std::u16string window_title, std::vector<MapLegendEntry> entries)
        : Window(std::move(name), style.window, style.size.x, style.size.y)
        , style_(style)
    {
        title = std::move(window_title);
        resizable = true;
        min_size = style.min_size;

        canvas_ = &content().add<MapCanvas>("map", style.canvas);
        canvas_->placement = { { 0, 0, 1, 1 }, { 0, 0, -(style.legend_width + style.gap), 0 } };

        const std::vector<float> tops = legend_tops(style_, entries);
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            MapLegendRow& row = content().add<MapLegendRow>(entries[i].name.empty() ? "layer" + std::to_string(i) : entries[i].name, style_, entries[i].colour, entries[i].shown, entries[i].round);
            row.placement = { { 1, 0, 1, 0 }, { -style.legend_width, tops[i], 0, tops[i] + style.row_height } };
            Label& label = row.add<Label>("text", std::move(entries[i].text), style.font);
            label.placement = { { 0, 0, 1, 1 }, { style.swatch + style.swatch_gap * 1.5f, 0, 0, 0 } };
            label.style.vertical = Align::Centre;
            rows_.push_back(&row);
            labels_.push_back(&label);
        }
    }

    bool MapPanel::shown(std::size_t index) const
    {
        return index < rows_.size() && rows_[index]->shown;
    }

    bool MapPanel::take_change()
    {
        bool changed = false;
        for (MapLegendRow* row : rows_)
        {
            changed = changed || row->changes != 0;
            row->changes = 0;
        }
        return changed;
    }

    void MapPanel::update(const UpdateContext& context)
    {
        Window::update(context);
        for (std::size_t i = 0; i < rows_.size(); ++i)
            labels_[i]->style.colour = rows_[i]->shown ? style_.text : style_.text_off;
    }

} // namespace rct3::ui
