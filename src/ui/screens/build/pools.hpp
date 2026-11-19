// SPDX-License-Identifier: GPL-3.0-or-later
//
// The round panel in the bottom-left corner of the park view that builds swimming pools, opened by the rides
// menu's pools button. It has the land panel's shape: a disc in the corner with tabs on a ring round it, and a
// strip running out to the right of the disc that shows the page of the tab out.
//
// Tabs, going round the ring: pool paths, extras, the pool editor, changing rooms and saved pools, as in the game;
// while the paths page is out two more stand before them, as on the path panel: a pin and a camera follow switch.
// The pin shows the page's controls and has the pointer start a trail at a side of the pool picked; unpinned,
// the controls hide and the pointer lays paths by dragging. The editor's
// page has three rows: the three depths of water, then the snap to ground switch and the surface height beside
// it, which only counts while the switch is off; under them the four kinds of edge; at the foot, laying water,
// laying tiles and the grid switch, with the delete tool at the right end. Water, tiles and delete are one
// choice, the depths another, the edges a third; picking an edge puts the water out, as in the game. The extras and
// changing room pages show their pictures on round buttons in two rows; picking one puts it out instead of the
// editor's tool, and opening the editor's tab brings that back. The paths page has the path panel's controls
// (paths.hpp) but its cut switch: three arrows that turn left, go straight on or turn right, five slopes
// from steps down to steps up, and a button that takes the last piece away; a column at its right end holds the
// ways between levels (ramps and the spiral staircase), picked like the extras. The disc shows the picture of what the tool lays now. The band along the strip's top tells what the
// button under the pointer does, else the tab's help.
//
// Tabs and buttons whose feature is not built are greyed. The panel keeps its choices while hidden, so it opens
// again as it was left. It keeps no park state; the owner hears through take_change() what the player chose.
#pragma once

#include "core/types.hpp"
#include "ui/screens/build/paths.hpp"
#include "ui/screens/info/panel.hpp"
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

namespace rct3::ui::screens::build
{

    /// The paths page's controls: the path panel's, in their order, but its cut switch.
    inline constexpr usize kPoolPathControls = kFootpathControls - 1;

    struct PoolPanelStyle
    {
        /// Each path control's picture when it can be used and when it cannot, and its help (strings keys).
        std::array<std::string, kPoolPathControls> path_icons;
        std::array<std::string, kPoolPathControls> path_unusable_icons;
        std::array<std::string, kPoolPathControls> path_help;
        info::CodedInfoPanel coded;
        std::string tab_face;         ///< Behind each tab's icon
        std::string close_icon;       ///< On the close button
        widgets::SpinnerStyle height; ///< The surface height
        /// From the screen's bottom edge to the disc's: the tool row and its open menu stay clear below.
        f32 bottom = info::info_panel::kDiscMargin;
        Color text_colour = hex("#ffffff");
        Color chosen = hex("#00000040"); ///< Behind the chosen buttons
    };

    /// The panel's tabs, in their order round the ring.
    enum class PoolPage : u8
    {
        Editor,
        Paths,
        Extras,
        Rooms,
        Load,
    };

    /// A choice picked on one of the pages that offer them.
    struct PickedChoice
    {
        PoolPage page = PoolPage::Extras;
        usize choice = 0; ///< Its place in the page's choices
        friend bool operator==(const PickedChoice&, const PickedChoice&) = default;
    };

    /// What the editor's tool does.
    enum class PoolMode : u8
    {
        Water,
        Tiles,
        Delete,
        Path, ///< The paths page's
    };

    /// The editor's choices.
    struct PoolEditor
    {
        PoolMode mode = PoolMode::Water;
        i32 depth = 2; ///< 1 shallow, 2 normal, 3 deep
        i32 edge = 0;  ///< 0 straight, 1 cut across, 2 tight curves, 3 loose curves
        bool grid = false;
        bool snap = true; ///< The surface sits on the ground; else at `height`
        i32 height = 0;   ///< Whole units
        friend bool operator==(const PoolEditor&, const PoolEditor&) = default;
    };

    /// An extra, changing room, ramp or staircase the player can pick.
    struct PoolChoice
    {
        std::string icon;
        std::u16string help;
    };

    namespace pool_panel
    {
        inline constexpr f32 kButton = 32;    ///< An editor button, its 40 unit picture drawn smaller
        inline constexpr f32 kEdgeWidth = 30; ///< An edge button, its 37 by 25 picture drawn smaller
        inline constexpr f32 kEdgeHeight = 20;
        inline constexpr f32 kRowGap = 3;       ///< Between the editor's rows
        inline constexpr f32 kGroupGap = 8;     ///< Between the groups of a row
        inline constexpr f32 kHeightWidth = 52; ///< Of the surface height's number
        inline constexpr f32 kHeightHeight = 16;
        inline constexpr i32 kHeightLowest = -100;
        inline constexpr i32 kHeightHighest = 100;
        inline constexpr f32 kEditorWidth = 3 * kButton + kGroupGap + kButton + 4 + kHeightWidth;
        inline constexpr f32 kCell = 40;      ///< A picture on the extras and changing room pages, with the room round it
        inline constexpr f32 kStairCell = 32; ///< Chosen by eye: a way between levels in the paths page's column, three to the strip's height
        inline constexpr usize kRows = 2;
        inline constexpr f32 kChoicesNarrowest = 5 * kCell; ///< Chosen by eye: room on a page with few choices for a long name and price in its band
        inline constexpr f32 kBodyLeft = 40;                ///< From the strip's left edge to the page, past the ring's tabs
        inline constexpr f32 kBodyRight = 16;
        inline constexpr f32 kBodyTop = 30;
        inline constexpr f32 kStatusGap = 8;
        inline constexpr f32 kDiscIcon = 56; ///< The picture of what the tool lays, on the disc
        inline constexpr f32 kCloseLeft = 9;
        inline constexpr f32 kCloseBottom = 9;
        /// Of the ring's slots (footpath_tab_offset), each tab's in the order of PoolPage, and the paths page's pin
        /// and camera follow switch.
        inline constexpr std::array<i32, 5> kTabSlots { 4, 2, 3, 5, 6 };
        inline constexpr i32 kPinSlot = 0;
        inline constexpr i32 kFollowSlot = 1;
    } // namespace pool_panel

    class PoolPanel : public Widget
    {
    public:
        PoolPanel(std::string name, PoolPanelStyle style, const StringTable& strings);

        /// Opens the panel with the `extras`, `rooms` and `stairs` (the ways between levels) to pick from, on the
        /// page last out (the editor's at first) or on `page`, the editor's tool out.
        void show(std::vector<PoolChoice> extras, std::vector<PoolChoice> rooms, std::vector<PoolChoice> stairs, std::optional<PoolPage> page = std::nullopt);
        void hide();
        [[nodiscard]] bool showing() const
        {
            return visible;
        }

        [[nodiscard]] const PoolEditor& editor() const
        {
            return editor_;
        }
        /// The extra, changing room or way between levels picked, by its page, out instead of the editor's tool.
        [[nodiscard]] std::optional<PickedChoice> placing() const
        {
            return placing_;
        }
        [[nodiscard]] std::optional<PoolPage> page() const
        {
            return page_;
        }

        /// The paths page's pin is in: its controls show and the pointer starts a trail at a side picked.
        [[nodiscard]] bool pinned() const
        {
            return pin_->on;
        }
        /// The camera follows the trail's end as pieces are laid.
        [[nodiscard]] bool following() const
        {
            return follow_->on;
        }
        /// Which of the paths page's controls can be used, and which of them picks the slope chosen.
        void set_path_controls(const std::array<bool, kPoolPathControls>& usable, usize slope_control)
        {
            path_usable_ = usable;
            path_slope_ = slope_control;
        }
        /// The paths page's control pressed since the last call, by its index.
        [[nodiscard]] std::optional<usize> take_path_control()
        {
            return std::exchange(path_control_now_, std::nullopt);
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

        /// Shows `texture` (a picture of what is picked, drawn this frame) in the disc instead of its icon, but
        /// while taking squares up.
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
        [[nodiscard]] const std::vector<PoolChoice>& choices_of(std::optional<PoolPage> page) const;

        PoolPanelStyle style_;
        const StringTable& strings_;
        widgets::Button* close_ = nullptr;
        Widget* body_ = nullptr;
        widgets::Label* status_ = nullptr;
        std::array<widgets::Button*, 5> tabs_ { };
        widgets::Button* pin_ = nullptr;
        widgets::Button* follow_ = nullptr;
        // The editor's page.
        Widget* editor_page_ = nullptr;
        std::array<widgets::Button*, 3> modes_ { };  ///< Water, tiles, delete
        std::array<widgets::Button*, 3> depths_ { }; ///< Shallow, normal, deep
        std::array<widgets::Button*, 4> edges_ { };  ///< Straight, diagonal, tight, loose
        // The paths' page.
        Widget* paths_page_ = nullptr;
        std::array<widgets::Button*, kPoolPathControls> path_controls_ { };
        std::array<bool, kPoolPathControls> path_usable_ { };
        usize path_slope_ = 5; ///< The control of the slope chosen
        std::optional<usize> path_control_now_;
        widgets::Button* grid_ = nullptr;
        widgets::Button* snap_ = nullptr;
        widgets::Spinner* height_ = nullptr;
        // The extras', changing rooms' and paths' pictures to pick.
        std::vector<widgets::Button*> choices_;
        std::vector<PoolChoice> extras_;
        std::vector<PoolChoice> rooms_;
        std::vector<PoolChoice> stairs_;
        std::optional<PoolPage> page_ = PoolPage::Editor;
        PoolEditor editor_;
        std::optional<PickedChoice> placing_;
        bool changed_ = false;
        TextureId view_ = 0;
        bool closed_ = false;
    };

} // namespace rct3::ui::screens::build
