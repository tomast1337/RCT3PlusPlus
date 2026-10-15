// SPDX-License-Identifier: GPL-3.0-or-later
//
// The round panel in the bottom-right corner of the park view that tells about one thing at a time: an
// attraction, and later a person. A disc holds a view of the thing; its tabs sit on a ring round the disc,
// each at its own place on the ring; the open tab's content slides out to the left of the disc in a strip
// with rounded ends. A small button under the disc closes the panel. The panel never moves.
//
// The panel does not know what it tells about. The owner gives it the tabs with show(), hears which tab the
// player picked through take_picked(), fills body() with that tab's content and gives its size with
// set_body_size(). Showing other tabs replaces the old ones, so only one thing is ever told about.
#pragma once

#include "ui/widgets/button.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rct3::ui
{

    struct InfoPanelStyle
    {
        std::string disc;       ///< Behind the view, fully opaque
        std::string glass;      ///< Over the view, faint
        std::string rim;        ///< Round the right of the disc
        std::string shadow;     ///< Under the disc
        std::string rim_shadow; ///< Under the rim
        std::string strip;      ///< Behind a tab's content, stretched in nine parts
        std::string close_icon; ///< On the close button
        std::string tab_face;   ///< Behind each tab's icon
        float glass_alpha = 0.15f;
    };

    /// A tab: its icon, its help, and its place on the ring.
    struct InfoTab
    {
        std::string icon;
        std::u16string help;
        int slot = 0;
    };

    namespace info_panel
    {
        inline constexpr float kDisc = 128;       ///< Width and height of the disc
        inline constexpr float kDiscMargin = 18;  ///< From the screen's right and bottom edges to the disc
        inline constexpr float kShadowOffset = 3; ///< Of the shadows, right and down
        inline constexpr float kRingRadius = 70;  ///< From the disc's middle to the tabs' middles
        inline constexpr float kFirstAngle = 45;  ///< Degrees up from the right, of slot 0
        inline constexpr float kSlotAngle = 30;   ///< Between neighbouring slots, going round to the left
        inline constexpr float kTabSize = 27;     ///< A tab's face at rest
        inline constexpr float kTabGrown = 40;    ///< Under the pointer
        inline constexpr float kIconSize = 40;    ///< Of the tab icons' own pictures
        inline constexpr float kCloseSize = 14;
        inline constexpr float kCloseRight = 9;    ///< From the screen's right edge
        inline constexpr float kCloseBottom = 9;   ///< From the screen's bottom edge
        inline constexpr float kStripOverlap = 13; ///< Of the strip under the disc
        inline constexpr float kStripBottom = 6;   ///< From the screen's bottom edge
        inline constexpr float kStripExtra = 31;   ///< Of the strip's width beyond its content's
        inline constexpr float kStripNarrowest = 131;
        inline constexpr float kStripHeight = 130; ///< The tallest a strip grows
        inline constexpr float kBodyLeft = 16;     ///< Of the content, from the strip's left edge
        inline constexpr float kBodyTop = 12;      ///< From the strip's top edge
        inline constexpr float kPageWidth = 280;   ///< Of a page of lines
        inline constexpr float kLineHeight = 15;
        inline constexpr float kValueColumn = 150; ///< From the page's left edge to where values start
    } // namespace info_panel

    struct InfoLine
    {
        std::u16string label;
        std::u16string value;
    };

    /// A tab's content: a title, lines of a label and a value, and a note under them.
    struct InfoPage
    {
        std::u16string title;
        std::vector<InfoLine> lines;
        std::u16string note;
    };

    /// The middle of the tab at `slot`, from the middle of the disc, with y down.
    [[nodiscard]] Point info_tab_offset(int slot);

    class InfoPanel : public Widget
    {
    public:
        InfoPanel(std::string name, InfoPanelStyle style);

        /// Opens the panel with `tabs`, the one at `picked` open.
        void show(std::vector<InfoTab> tabs, std::size_t picked);
        void hide();
        [[nodiscard]] bool showing() const
        {
            return visible;
        }

        /// The tab the player opened since the last call.
        [[nodiscard]] std::optional<std::size_t> take_picked()
        {
            return std::exchange(picked_now_, std::nullopt);
        }
        /// True once after the player closed the panel.
        [[nodiscard]] bool take_closed()
        {
            return std::exchange(closed_, false);
        }
        [[nodiscard]] std::size_t picked() const
        {
            return picked_;
        }

        /// Where the open tab's content goes; the owner empties and fills it.
        [[nodiscard]] Widget& body() const
        {
            return *body_;
        }
        /// The size of the open tab's content; the strip fits round it.
        void set_body_size(float width, float height);
        /// Fills the body with `page`: its title, its lines in two columns and its note.
        void show_page(const InfoPage& page);

        [[nodiscard]] Rect disc() const;
        [[nodiscard]] Rect strip() const;
        [[nodiscard]] Button& tab(std::size_t i) const
        {
            return *tabs_.at(i);
        }
        [[nodiscard]] std::size_t tab_count() const
        {
            return tabs_.size();
        }

        /// The disc and the strip stop the pointer; the rest of the screen lets it through.
        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        [[nodiscard]] bool covers(Point p) const override;
        void update(const UpdateContext& context) override;
        void draw(DrawContext& context) const override;

    private:
        void place();
        void mark_picked();

        InfoPanelStyle style_;
        std::vector<Button*> tabs_;
        std::vector<int> slots_;
        Button* close_ = nullptr;
        Widget* body_ = nullptr;
        std::size_t picked_ = 0;
        std::optional<std::size_t> picked_now_;
        bool closed_ = false;
        float body_width_ = 0;
        float body_height_ = 0;
    };

} // namespace rct3::ui
