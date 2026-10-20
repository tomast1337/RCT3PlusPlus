// SPDX-License-Identifier: GPL-3.0-or-later
//
// One of the park's management windows (park operations, finances, staff and so on) in the game's large
// frame: its icon in the corner, its name as the title, picture tabs in the top-right corner with the open
// tab's name on their left, and under them the open tab's page.
//
// A page is lines of a label and a value, then a graph, then a table of rows under headings, then a row of
// buttons; any of them may be missing. The table fills what the rest leaves of the window. The window does not
// know what it shows: the owner hears which tab the player picked through take_tab() and gives the page with
// show_page(), again whenever what it shows has changed. The window sorts the table's rows itself, by the
// heading the player pressed last on that tab, and keeps the scroll and the selection across new pages.
#pragma once

#include "core/maybe.hpp"
#include "ui/screens/info_panel.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/column_table.hpp"
#include "ui/widgets/label.hpp"
#include "ui/widgets/line_plot.hpp"
#include "ui/widgets/window.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rct3::ui
{

    struct ManagementWindowStyle
    {
        WindowStyle window;
        ColumnTableStyle table;
        ScrollBarStyle scroll;
        float tab_size = 23;
        float tab_step = 20; ///< The tabs overlap a little, as the options window's do
        float tab_bar_height = 25;
        float caption_x = 59; ///< Right of the window's icon, under the title
        float caption_height = 20;
        FontRole caption_font = FontRole::Small;
        Color caption_colour = hex("#2484be");
        Color text_colour = hex("#000000");
        Color heading_colour = hex("#2484be"); ///< Of a line that has no value: a heading over the lines below it
        float line_height = 15;
        float value_column = 0.55f; ///< Of the page's width, where values start
        float pair_gap = 12;        ///< Between the two half lines of a row
        float plot_height = 90;
        float gap = 6;               ///< Between the parts of a page
        std::string button;          ///< Skin family of the buttons under a page
        Rect button_inset;           ///< Of their text; see Button::text_inset
        float button_height = 22;    ///< Of a button with text
        float icon_button_size = 34; ///< Width and height of a button with an icon and no text
        /// The colours of a graph's series, in turn.
        std::vector<Color> series_colours { hex("#2484be"), hex("#e0402a"), hex("#3aa83a"), hex("#e0a020"), hex("#8a4fc0"), hex("#20a0a0") };
        Color plot_background = hex("#ffffffa0");
        Color plot_zero = hex("#00000040");
    };

    /// A tab: the skin of its picture and its name, shown as its help and beside the tabs while it is open.
    struct ManagementTab
    {
        std::string icon;
        std::u16string name;
    };

    /// A button under a page: a picture or a text, and its help.
    struct ManagementButton
    {
        std::string icon;
        std::u16string text;
        std::u16string help;
        bool enabled = true;
    };

    struct ManagementPage
    {
        std::vector<InfoLine> lines;
        std::vector<InfoSeries> graph;
        std::vector<TableColumn> columns; ///< No table without any
        std::vector<TableRow> rows;
        std::optional<std::size_t> sort; ///< The column the rows are sorted by until the player picks one
        bool sort_descending = false;
        std::vector<ManagementButton> buttons;
        std::u16string note; ///< Under everything, across the page
    };

    class ManagementWindow : public Window
    {
    public:
        /// `width` x `height` with the frame, centred on the desktop, `tabs` along the top; `picked` open.
        ManagementWindow(std::string name, const ManagementWindowStyle& style, float width, float height, std::vector<ManagementTab> tabs, std::size_t picked);

        [[nodiscard]] std::size_t tab() const
        {
            return tab_;
        }
        [[nodiscard]] std::size_t tab_count() const
        {
            return tabs_.size();
        }
        [[nodiscard]] Button& tab_button(std::size_t i) const
        {
            return *tabs_.at(i);
        }
        /// Opens tab `i`, as if the player picked it, but without reporting it.
        void open_tab(std::size_t i);
        /// The tab the player picked since the last call.
        [[nodiscard]] std::optional<std::size_t> take_tab()
        {
            return std::exchange(tab_now_, std::nullopt);
        }
        /// The button under the page the player pressed since the last call.
        [[nodiscard]] std::optional<std::size_t> take_button()
        {
            return std::exchange(button_now_, std::nullopt);
        }
        /// The id of the table row the player activated since the last call.
        [[nodiscard]] std::optional<std::size_t> take_row()
        {
            return std::exchange(row_now_, std::nullopt);
        }

        void show_page(const ManagementPage& page);
        /// Seconds since the last page came.
        [[nodiscard]] float page_age() const
        {
            return page_age_;
        }
        [[nodiscard]] std::optional<ColumnTable&> table() const
        {
            return maybe(table_);
        }

        void update(const UpdateContext& context) override;

    private:
        /// The page's area inside the window's content, below the tabs.
        [[nodiscard]] Widget& page() const
        {
            return *page_;
        }
        void sort_rows();

        ManagementWindowStyle style_;
        std::vector<Button*> tabs_;
        std::vector<std::u16string> names_;
        Label* caption_ = nullptr;
        Widget* page_ = nullptr;
        Widget* lines_ = nullptr; ///< The page's lines, graph, buttons and note, made again with each page
        ColumnTable* table_ = nullptr;
        std::vector<Button*> buttons_;
        /// Of each tab, the column the player sorted by and whether highest first; none until they pick one.
        std::vector<std::optional<std::pair<std::size_t, bool>>> sorts_;
        std::optional<std::pair<std::size_t, bool>> page_sort_; ///< The page's own, for when the player has none
        std::size_t tab_ = 0;
        float page_age_ = 0;
        std::optional<std::size_t> tab_now_;
        std::optional<std::size_t> button_now_;
        std::optional<std::size_t> row_now_;
    };

} // namespace rct3::ui
