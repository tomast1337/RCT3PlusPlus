// SPDX-License-Identifier: GPL-3.0-or-later
//
// The round panel in the bottom-left corner of the park view that lays paths, opened by the tool row's paths
// button. It is the info panel's shape turned the other way: a disc in the corner with tabs on a ring round
// it, and a strip running out to the right of the disc that shows one page at a time.
//
// Tabs, going round the ring: one shows the building controls, one keeps the camera on the build point while
// building, and two open the path styles and the queue styles. A style page shows each style's picture; the
// one chosen is marked, and choosing one goes back to the controls. The controls are three arrows that turn
// left, go straight on or turn right, five slopes from steps down to steps up, a switch to cut into the
// ground and a button that takes the last piece away. A line under them tells what the control under the
// pointer does. The band along the strip's top shows what the owner gives it, such as the chosen style and its
// price. The disc shows the chosen style's picture.
//
// The panel keeps no park state. The owner gives it the styles with show(), says which controls can be used
// with set_controls(), and hears through take_control() and take_style() what the player pressed.
#pragma once

#include "ui/panel_look.hpp"
#include "ui/screens/info_panel.hpp"
#include "ui/string_table.hpp"
#include "ui/widgets/button.hpp"
#include "ui/widgets/label.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rct3::ui
{

    /// The panel's controls, in the order of park::PathControl.
    inline constexpr std::size_t kFootpathControls = 10;

    struct FootpathPanelStyle
    {
        PanelLook look = PanelLook::Coded;
        CodedInfoPanel coded;
        std::string tab_face;   ///< Behind each tab's icon
        std::string close_icon; ///< On the close button
        std::string controls_icon;
        std::string follow_icon;
        std::string paths_icon;
        std::string queues_icon;
        /// Strings keys of the tabs' help, in the order above.
        std::array<std::string, 4> tab_help;
        /// Each control's picture when it can be used and when it cannot; the cut switch has its own two
        /// pictures instead, for on and off.
        std::array<std::string, kFootpathControls> usable_icons;
        std::array<std::string, kFootpathControls> unusable_icons;
        std::string cut_on_icon;
        std::string cut_off_icon;
        std::array<std::string, kFootpathControls> control_help; ///< Strings keys
        Color text_colour = hex("#ffffff");
        Color chosen = hex("#ffffff60"); ///< Behind the chosen style on a style page
    };

    namespace footpath_panel
    {
        /// Of the ring's slots, how far round each tab sits: the first is up and to the left of the disc's
        /// middle, the others follow clockwise.
        inline constexpr float kFirstAngle = 128;
        inline constexpr float kSlotAngle = -32;
        inline constexpr int kControlsSlot = 0;
        inline constexpr int kFollowSlot = 1;
        inline constexpr int kPathsSlot = 4;
        inline constexpr int kQueuesSlot = 5;
        inline constexpr float kCloseLeft = 9; ///< From the screen's left edge
        inline constexpr float kCloseBottom = 9;
        /// From the strip's left edge to the page: the ring's tabs on the right of the disc stand over the room
        /// left before it, as they do in the game's own panel.
        inline constexpr float kBodyLeft = 40;
        inline constexpr float kBodyRight = 16; ///< After the page
        inline constexpr float kBodyTop = 22;   ///< From the strip's top edge, below its band
        inline constexpr float kStatusGap = 8;  ///< Between the disc and the band's text
        /// The controls page and the rectangle of each control on it, in the order of the controls.
        inline constexpr float kControlsWidth = 160;
        inline constexpr float kControlsHeight = 100;
        inline constexpr std::array<Rect, kFootpathControls> kControlRects { {
            { 32, 4, 60, 30 },
            { 62, 4, 90, 30 },
            { 92, 4, 120, 30 },
            { 2, 34, 30, 62 },
            { 32, 34, 60, 62 },
            { 62, 34, 90, 62 },
            { 92, 34, 120, 62 },
            { 122, 34, 150, 62 },
            { 122, 68, 158, 98 },
            { 82, 68, 118, 98 },
        } };
        inline constexpr Rect kTextRect { 0, 68, 80, 98 };
        inline constexpr float kLargeIcon = 40; ///< Of the remove and cut pictures, drawn smaller to fit
        inline constexpr std::size_t kStyleColumns = 5;
        inline constexpr float kStyleCell = 36; ///< A style's picture and the room round it
        inline constexpr float kStyleIcon = 40; ///< Of the styles' own pictures
        inline constexpr float kLineHeight = 15;
        inline constexpr float kDiscIcon = 56; ///< The chosen style's picture on the disc
    } // namespace footpath_panel

    /// The middle of the tab at `slot`, from the middle of the disc, with y down.
    [[nodiscard]] Point footpath_tab_offset(int slot);

    /// A style the player can choose.
    struct FootpathStyle
    {
        std::string icon;
        std::u16string name;
        std::u16string price; ///< As written beside its name
    };

    /// Which page the strip shows.
    enum class FootpathPage : std::uint8_t
    {
        None,
        Controls,
        Paths,
        Queues,
    };

    /// A style the player chose: a queue style or a path style, and which of them.
    struct FootpathPick
    {
        bool queue = false;
        std::size_t index = 0;
    };

    class FootpathPanel : public Widget
    {
    public:
        FootpathPanel(std::string name, FootpathPanelStyle style, const StringTable& strings);

        /// Opens the panel with the styles to choose from and the controls page out.
        void show(std::vector<FootpathStyle> paths, std::vector<FootpathStyle> queues);
        void hide();
        [[nodiscard]] bool showing() const
        {
            return visible;
        }

        /// Marks the chosen style; none when `pick` is empty.
        void set_chosen(std::optional<FootpathPick> pick);
        [[nodiscard]] std::optional<FootpathPick> chosen() const
        {
            return chosen_;
        }
        /// Which controls can be used, which slope is picked and whether cutting into the ground is on.
        void set_controls(const std::array<bool, kFootpathControls>& usable, std::size_t slope_control, bool cut);
        /// What the band along the strip's top shows while the controls are out.
        void set_status(std::u16string text);

        void open_page(FootpathPage page);
        [[nodiscard]] FootpathPage page() const
        {
            return page_;
        }
        [[nodiscard]] bool following() const
        {
            return follow_->on;
        }

        /// The control pressed since the last call, by its index.
        [[nodiscard]] std::optional<std::size_t> take_control()
        {
            return std::exchange(control_now_, std::nullopt);
        }
        /// The style chosen since the last call.
        [[nodiscard]] std::optional<FootpathPick> take_style()
        {
            return std::exchange(style_now_, std::nullopt);
        }
        /// True once after the player closed the panel.
        [[nodiscard]] bool take_closed()
        {
            return std::exchange(closed_, false);
        }

        [[nodiscard]] Rect disc() const;
        [[nodiscard]] Rect strip() const;
        [[nodiscard]] Button& control(std::size_t i) const
        {
            return *controls_.at(i);
        }
        [[nodiscard]] Button& tab(FootpathPage page) const;
        [[nodiscard]] Button& follow_tab() const
        {
            return *follow_;
        }
        [[nodiscard]] std::size_t style_count() const
        {
            return styles_.size();
        }
        [[nodiscard]] Button& style_button(std::size_t i) const
        {
            return *styles_.at(i);
        }
        [[nodiscard]] const Label& text_line() const
        {
            return *text_;
        }
        [[nodiscard]] const Label& status_line() const
        {
            return *status_line_;
        }

        [[nodiscard]] bool takes_pointer() const override
        {
            return true;
        }
        [[nodiscard]] bool covers(Point p) const override;
        void update(const UpdateContext& context) override;
        void draw(DrawContext& context) const override;

    private:
        [[nodiscard]] Point body_size() const;
        void place();
        void fill_styles();
        void mark();

        FootpathPanelStyle style_;
        const StringTable& strings_;
        Button* close_ = nullptr;
        Button* controls_tab_ = nullptr;
        Button* follow_ = nullptr;
        Button* paths_tab_ = nullptr;
        Button* queues_tab_ = nullptr;
        Widget* body_ = nullptr;
        std::array<Button*, kFootpathControls> controls_ { };
        std::array<bool, kFootpathControls> usable_ { };
        Label* text_ = nullptr;
        Label* status_line_ = nullptr;
        Label* style_name_ = nullptr;
        std::vector<Button*> styles_;
        std::vector<FootpathStyle> paths_;
        std::vector<FootpathStyle> queues_;
        std::u16string status_;
        std::optional<FootpathPick> chosen_;
        std::optional<std::size_t> control_now_;
        std::optional<FootpathPick> style_now_;
        FootpathPage page_ = FootpathPage::Controls;
        std::size_t slope_ = 5; ///< The control of the picked slope
        bool cut_ = false;
        bool closed_ = false;
    };

} // namespace rct3::ui
