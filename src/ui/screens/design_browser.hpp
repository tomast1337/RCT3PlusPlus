// SPDX-License-Identifier: GPL-3.0-or-later
//
// The window that offers the saved designs of a tracked ride's kind before its track is built, as the game's
// does. On the left the designs as a list sorted by the heading pressed, the row that builds a custom track
// always first; on the right a picture of the picked design's track from above, what it measured (its
// ground, price, ratings and test results), and the train it will be built with, which the arrows beside
// its name step through. Along the bottom "Build this" and "Cancel".
//
// The window knows nothing of the designs. The owner gives the rows (the custom track's row with id 0 and the
// designs' after it); hears with take_picked() which row was picked and answers with show_design(); hears
// with take_train_step() an arrow pressed and answers with show_train(); and hears with take_build() the
// row to build, from the button or a row double-clicked. A locked row cannot be built: the owner gives its
// lines as the reason and switches the button off with show_design().
#pragma once

#include "ui/draw_list.hpp"
#include "ui/screens/info_panel.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/column_table.hpp"
#include "ui/widgets/label.hpp"
#include "ui/widgets/window.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rct3::ui
{

    struct DesignBrowserStyle
    {
        WindowStyle window;
        ColumnTableStyle table;
        ScrollBarStyle scroll;
        std::string button;   ///< Skin family of the text buttons
        Rect button_inset;    ///< Of their text; see Button::text_inset
        std::string previous; ///< Skins of the train's arrows
        std::string next;
        std::string border; ///< Around the picture
        Color text_colour = hex("#000000");
        // Our own layout, in units.
        float width = 780;
        float height = 560;
        float list_share = 0.6f; ///< Of the content's width
        float gap = 8;
        float picture = 140; ///< Width and height
        float line_height = 14;
        float value_column = 0.66f; ///< Of the details' width
        float button_width = 100;
        float button_height = 22;
        float arrow = 20;
    };

    class DesignBrowser : public Window
    {
    public:
        /// `title` on the frame, `icon` in its corner; `columns` over `rows`, sorted by the first column.
        DesignBrowser(std::string name, const DesignBrowserStyle& style, std::u16string title, std::string icon, std::vector<TableColumn> columns, std::vector<TableRow> rows, std::u16string build_text, std::u16string cancel_text);

        /// The id of the row picked since the last call.
        [[nodiscard]] std::optional<std::size_t> take_picked()
        {
            return std::exchange(picked_, std::nullopt);
        }
        /// -1 or 1 for each press of the train's arrows since the last call, the last one.
        [[nodiscard]] std::optional<int> take_train_step()
        {
            return std::exchange(train_step_, std::nullopt);
        }
        /// The id of the row to build, once asked.
        [[nodiscard]] std::optional<std::size_t> take_build()
        {
            return std::exchange(build_, std::nullopt);
        }
        /// Whether Cancel was pressed, once.
        [[nodiscard]] bool take_cancel()
        {
            return std::exchange(cancel_, false);
        }

        /// Shows the picked design: `picture` (0 for none) and `lines`; whether it may be built.
        void show_design(TextureId picture, std::vector<InfoLine> lines, bool buildable);
        /// Shows the train's name; the arrows only when there is more than one train to step through.
        void show_train(std::u16string name, bool steps);

        [[nodiscard]] ColumnTable& table() const
        {
            return *table_;
        }

        void update(const UpdateContext& context) override;
        void draw(DrawContext& context) const override;

    private:
        DesignBrowserStyle style_;
        ColumnTable* table_ = nullptr;
        Widget* details_ = nullptr; ///< The picture's place, then the lines, made again with each design
        Widget* lines_ = nullptr;
        Label* train_ = nullptr;
        Button* previous_ = nullptr;
        Button* next_ = nullptr;
        Button* build_button_ = nullptr;
        Button* cancel_button_ = nullptr;
        TextureId picture_ = 0;
        std::optional<std::size_t> shown_; ///< Id of the row the details are of
        std::optional<std::size_t> picked_;
        std::optional<int> train_step_;
        std::optional<std::size_t> build_;
        bool cancel_ = false;
    };

} // namespace rct3::ui
