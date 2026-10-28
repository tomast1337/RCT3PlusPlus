// SPDX-License-Identifier: GPL-3.0-or-later
//
// The round panel in the bottom-left corner of the park view that builds swimming pools, opened by the rides
// menu's pools button. It has the land panel's shape: a disc in the corner with tabs on a ring round it, and a
// strip running out to the right of the disc that shows the page of the tab out.
//
// Tabs, going round the ring: the pool editor, pool paths, extras, changing rooms and saved pools. The editor's
// page has three rows: the three depths of water, then the snap to ground switch and the surface height beside
// it, which only counts while the switch is off; under them the four kinds of edge; at the foot, laying water,
// laying tiles and the grid switch, with the delete tool at the right end. Water, tiles and delete are one
// choice, the depths another, the edges a third. The extras and changing room pages show their pictures in two
// rows; picking one puts it out instead of the editor's tool, and opening the editor's tab brings that back.
// The disc shows the picture of what the tool lays now. The band along the strip's top tells what the button
// under the pointer does, else the tab's help.
//
// Tabs and buttons whose feature is not built are greyed. The panel keeps its choices while hidden, so it opens
// again as it was left. It keeps no park state; the owner hears through take_change() what the player chose.
#pragma once

#include "ui/screens/info_panel.hpp"
#include "ui/string_table.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/label.hpp"
#include "ui/widgets/spinner.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rct3::ui
{

    struct PoolPanelStyle
    {
        CodedInfoPanel coded;
        std::string tab_face;   ///< Behind each tab's icon
        std::string close_icon; ///< On the close button
        SpinnerStyle height;    ///< The surface height
        /// From the screen's bottom edge to the disc's: the tool row and its open menu stay clear below.
        float bottom = info_panel::kDiscMargin;
        Color text_colour = hex("#ffffff");
        Color chosen = hex("#00000040"); ///< Behind the chosen buttons
    };

    /// The panel's tabs, in their order round the ring.
    enum class PoolPage : std::uint8_t
    {
        Editor,
        Paths,
        Extras,
        Rooms,
        Load,
    };

    /// What the editor's tool does.
    enum class PoolMode : std::uint8_t
    {
        Water,
        Tiles,
        Delete,
    };

    /// The editor's choices.
    struct PoolEditor
    {
        PoolMode mode = PoolMode::Water;
        std::int32_t depth = 2; ///< 1 shallow, 2 normal, 3 deep
        bool grid = false;
        bool snap = true;        ///< The surface sits on the ground; else at `height`
        std::int32_t height = 0; ///< Whole units
        friend bool operator==(const PoolEditor&, const PoolEditor&) = default;
    };

    /// An extra or changing room the player can pick.
    struct PoolChoice
    {
        std::string icon;
        std::u16string help;
    };

    namespace pool_panel
    {
        inline constexpr float kButton = 32;    ///< An editor button, its 40 unit picture drawn smaller
        inline constexpr float kEdgeWidth = 30; ///< An edge button, its 37 by 25 picture drawn smaller
        inline constexpr float kEdgeHeight = 20;
        inline constexpr float kRowGap = 3;       ///< Between the editor's rows
        inline constexpr float kGroupGap = 8;     ///< Between the groups of a row
        inline constexpr float kHeightWidth = 52; ///< Of the surface height's number
        inline constexpr float kHeightHeight = 16;
        inline constexpr std::int32_t kHeightLowest = -100;
        inline constexpr std::int32_t kHeightHighest = 100;
        inline constexpr float kEditorWidth = 3 * kButton + kGroupGap + kButton + 4 + kHeightWidth;
        inline constexpr float kCell = 40; ///< A picture on the extras and changing room pages, with the room round it
        inline constexpr std::size_t kRows = 2;
        inline constexpr float kBodyLeft = 40; ///< From the strip's left edge to the page, past the ring's tabs
        inline constexpr float kBodyRight = 16;
        inline constexpr float kBodyTop = 30;
        inline constexpr float kStatusGap = 8;
        inline constexpr float kDiscIcon = 56; ///< The picture of what the tool lays, on the disc
        inline constexpr float kCloseLeft = 9;
        inline constexpr float kCloseBottom = 9;
        inline constexpr int kFirstSlot = 1; ///< Of the ring's slots, the editor's tab's; the others follow
    } // namespace pool_panel

    class PoolPanel : public Widget
    {
    public:
        PoolPanel(std::string name, PoolPanelStyle style, const StringTable& strings);

        /// Opens the panel with the `extras` and `rooms` to pick from, on the page last out (the editor's at
        /// first), the editor's tool out.
        void show(std::vector<PoolChoice> extras, std::vector<PoolChoice> rooms);
        void hide();
        [[nodiscard]] bool showing() const
        {
            return visible;
        }

        [[nodiscard]] const PoolEditor& editor() const
        {
            return editor_;
        }
        /// The extra or changing room picked, out instead of the editor's tool.
        [[nodiscard]] std::optional<std::pair<PoolPage, std::size_t>> placing() const
        {
            return placing_;
        }
        [[nodiscard]] std::optional<PoolPage> page() const
        {
            return page_;
        }

        /// True once after the player changed what the tool does.
        [[nodiscard]] bool take_change()
        {
            return std::exchange(changed_, false);
        }
        /// True once after the player closed the panel.
        [[nodiscard]] bool take_closed()
        {
            return std::exchange(closed_, false);
        }

        /// Shows `texture` (a picture of what is picked, drawn this frame) in the disc instead of its icon, while
        /// an extra or changing room is picked.
        void show_view(TextureId texture)
        {
            view_ = texture;
        }

        [[nodiscard]] Rect disc() const;
        [[nodiscard]] Rect strip() const;

        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        [[nodiscard]] bool covers(Point p) const override;
        void update(const UpdateContext& context) override;
        void draw(DrawContext& context) const override;

    private:
        void open(std::optional<PoolPage> page);
        void place();
        [[nodiscard]] Point body_size() const;
        [[nodiscard]] std::string_view preview() const;

        PoolPanelStyle style_;
        const StringTable& strings_;
        Button* close_ = nullptr;
        Widget* body_ = nullptr;
        Label* status_ = nullptr;
        std::array<Button*, 5> tabs_ { };
        // The editor's page.
        Widget* editor_page_ = nullptr;
        std::array<Button*, 3> modes_ { };  ///< Water, tiles, delete
        std::array<Button*, 3> depths_ { }; ///< Shallow, normal, deep
        std::array<Button*, 4> edges_ { };  ///< Straight, diagonal, tight, loose
        Button* grid_ = nullptr;
        Button* snap_ = nullptr;
        Spinner* height_ = nullptr;
        // The extras' and changing rooms' pages.
        std::vector<Button*> choices_;
        std::vector<PoolChoice> extras_;
        std::vector<PoolChoice> rooms_;
        std::optional<PoolPage> page_ = PoolPage::Editor;
        PoolEditor editor_;
        std::optional<std::pair<PoolPage, std::size_t>> placing_;
        bool changed_ = false;
        TextureId view_ = 0;
        bool closed_ = false;
    };

} // namespace rct3::ui
