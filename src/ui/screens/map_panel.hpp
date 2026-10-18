// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park map window: the map on the left (map_canvas.hpp) and a legend down the right. Each legend row
// names a layer next to a swatch of its colour; clicking the row shows or hides the layer, and a hidden
// layer's row is faded with an empty swatch. The window can be moved and resized; the map takes whatever
// room the legend leaves and keeps its shape inside it.
//
// The window knows nothing of the park: the owner names the rows, reads which are on and fills the map.
#pragma once

#include "ui/screens/map_canvas.hpp"
#include "ui/widgets/label.hpp"
#include "ui/widgets/window.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace rct3::ui
{

    struct MapPanelStyle
    {
        WindowStyle window;
        MapCanvasStyle canvas;
        Point size { 640, 500 };     ///< At first: about square for the map
        Point min_size { 460, 420 }; ///< Room for the whole legend
        float legend_width = 170;
        float gap = 8; ///< Between the map and the legend
        float row_height = 20;
        float group_gap = 8; ///< Above a row that starts a group
        float swatch = 12;
        float swatch_gap = 8;
        FontRole font = FontRole::Small;
        Color text = hex("#1b1b1b");
        Color text_off = hex("#1b1b1b70");
        Color hover = hex("#3c78d830");
        Color swatch_line = hex("#00000060");
    };

    struct MapLegendEntry
    {
        std::u16string text;
        Color colour;
        bool shown = true;
        bool starts_group = false;
        bool round = false;     ///< A round swatch, for things the map shows as dots
        std::string name = { }; ///< Its row's name: the text's string key, to find where it is made by
    };

    /// Where the legend's rows go, from the top of the legend: each row's top.
    [[nodiscard]] std::vector<float> legend_tops(const MapPanelStyle& style, const std::vector<MapLegendEntry>& entries);

    class MapLegendRow;

    class MapPanel : public Window
    {
    public:
        MapPanel(std::string name, const MapPanelStyle& style, std::u16string title, std::vector<MapLegendEntry> entries);

        [[nodiscard]] MapCanvas& canvas()
        {
            return *canvas_;
        }
        /// Whether the layer of row `index` is shown.
        [[nodiscard]] bool shown(std::size_t index) const;
        [[nodiscard]] std::size_t size() const
        {
            return rows_.size();
        }
        /// True once for each time rows were turned on or off since the last call.
        [[nodiscard]] bool take_change();

        void update(const UpdateContext& context) override;

    private:
        MapPanelStyle style_;
        MapCanvas* canvas_ = nullptr;
        std::vector<MapLegendRow*> rows_;
        std::vector<Label*> labels_;
    };

} // namespace rct3::ui
