// SPDX-License-Identifier: GPL-3.0-or-later
//
// The skins the game's own interface is drawn with, by the names its data uses, and the measurements that
// go with them. The measurements are ours, chosen to fit the skins.
#pragma once

#include "ui/font_set.hpp"
#include "ui/screens/options_dialog.hpp"
#include "ui/tooltip.hpp"
#include "ui/widgets/dropdown.hpp"
#include "ui/widgets/list_box.hpp"
#include "ui/widgets/menu.hpp"
#include "ui/widgets/menu_bar.hpp"
#include "ui/widgets/question_box.hpp"
#include "ui/widgets/scroll_bar.hpp"
#include "ui/widgets/slider.hpp"
#include "ui/widgets/spinner.hpp"
#include "ui/widgets/tabs.hpp"
#include "ui/widgets/toolbar.hpp"
#include "ui/widgets/window.hpp"

#include <string_view>
#include <utility>

namespace rct3::ui::game_skins
{

    inline constexpr std::string_view kWindowFrame = "GUI2Dialog_Frame";
    inline constexpr std::string_view kWindowFrameUnfocused = "GUI2Dialog_FrameNoFocus";
    inline constexpr std::string_view kWindowBackground = "GUI2MiniDialog_Background";
    inline constexpr std::string_view kTitleButton = "GUI2Dialog_TitleButton"; ///< Family
    inline constexpr std::string_view kCloseIcon = "GUI2Dialog_CloseIcon";
    inline constexpr std::string_view kButton = "GUI2Button";
    inline constexpr std::string_view kConfirmIcon = "GUI2MiniDialog_Confirm";
    inline constexpr std::string_view kCancelIcon = "GUI2MiniDialog_Cancel";
    inline constexpr std::string_view kTickbox = "Tickbox_Up";
    inline constexpr std::string_view kTick = "Tickbox_Tick";
    inline constexpr std::string_view kListBox = "EditBox_NoFocus";
    inline constexpr std::string_view kListBoxFocused = "EditBox_Focus";
    inline constexpr std::string_view kSelectedRow = "GUI2SolidBlueBackground";
    inline constexpr std::string_view kTooltip = "GUI2Swatch_Background";
    inline constexpr std::string_view kMenuBackground = "GUIMenuSurround";
    inline constexpr std::string_view kDropdownOpener = "ListBoxOpener"; ///< Family: "ListBoxOpenerDisabled"
    inline constexpr std::string_view kSliderThumb = "LeftRight_SliderArrows";
    inline constexpr std::string_view kSpinnerUp = "SpinnerUp"; ///< Family: "SpinnerUpDisabled"
    inline constexpr std::string_view kSpinnerDown = "SpinnerDown";
    inline constexpr std::string_view kScrollUp = "ScrollBar_UpArrow";
    inline constexpr std::string_view kScrollDown = "ScrollBar_DownArrow";
    inline constexpr std::string_view kScrollLeft = "ScrollBar_LeftArrow";
    inline constexpr std::string_view kScrollRight = "ScrollBar_RightArrow";
    inline constexpr std::string_view kVerticalRail = "VScrollBar_Rail";
    inline constexpr std::string_view kHorizontalRail = "HScrollBar_Rail";
    inline constexpr std::string_view kVerticalRider = "VScrollBar_Rider";   ///< Family
    inline constexpr std::string_view kHorizontalRider = "HScrollBar_Rider"; ///< Family
    /// The game passes text buttons their skin case by case; this lozenge is our default for them.
    inline constexpr std::string_view kTextButton = "GUI2Swatch_Background";
    /// What the lozenge's border and drop shadow take from each side (ours, measured on the skin).
    inline constexpr Rect kLozengeInset { 3, 2, 5, 5 };

    [[nodiscard]] inline WindowStyle window_style()
    {
        WindowStyle s;
        s.frame = kWindowFrame;
        s.frame_unfocused = kWindowFrameUnfocused;
        s.background = kWindowBackground;
        s.title_button = kTitleButton;
        s.close_icon = kCloseIcon;
        // The frame's top-left corner holds the window's icon and the title rule; content goes below it.
        s.content_inset = { 12, 70, 12, 12 };
        s.title_box = { 62, 20, 30, 40 }; // Right of the icon, on the title rule
        return s;
    }

    /// The game's scroll bars: 15 units thick with square arrows, 20 units per arrow press. The wheel step
    /// and the arrow repeat timing are ours.
    [[nodiscard]] inline ScrollBarStyle scroll_bar_style(Orientation orientation)
    {
        const bool vertical = orientation == Orientation::Vertical;
        ScrollBarStyle s;
        s.back_arrow = vertical ? kScrollUp : kScrollLeft;
        s.forward_arrow = vertical ? kScrollDown : kScrollRight;
        s.rail = vertical ? kVerticalRail : kHorizontalRail;
        s.rider = vertical ? kVerticalRider : kHorizontalRider;
        s.thickness = 15;
        s.step = 20;
        return s;
    }

    /// A list in the edit box frame, which turns blue with the keyboard focus, the selected row on the
    /// rounded blue patch. The row height is ours, to fit the small font.
    [[nodiscard]] inline ListBoxStyle list_box_style()
    {
        ListBoxStyle s;
        s.background = kListBox;
        s.focused = kListBoxFocused;
        s.selection_skin = kSelectedRow;
        s.font = FontRole::Small;
        s.row_height = 18;
        return s;
    }

    /// Tabs as text lozenges, the current one drawn down. The game's tab strips are rows of text buttons;
    /// the sizes are ours.
    [[nodiscard]] inline TabsStyle tabs_style()
    {
        TabsStyle s;
        s.tab = kTextButton;
        s.text_inset = kLozengeInset;
        s.font = FontRole::Small;
        s.bar_height = 24;
        return s;
    }

    /// The green double arrow at the size the skin has, on a line in the frames' blue (ours).
    [[nodiscard]] inline SliderStyle slider_style()
    {
        SliderStyle s;
        s.thumb = kSliderThumb;
        s.rail_colour = { 20, 100, 160, 255 };
        s.rail_thickness = 4;
        s.thumb_width = 18;
        s.thumb_height = 13;
        return s;
    }

    /// The number in the edit box frame, the two arrows stacked at their size.
    [[nodiscard]] inline SpinnerStyle spinner_style()
    {
        SpinnerStyle s;
        s.field = kListBox;
        s.field_focused = kListBoxFocused;
        s.up = kSpinnerUp;
        s.down = kSpinnerDown;
        s.arrow_width = 20;
        return s;
    }

    /// Menus on the white rounded surround, the highlighted row on the blue patch, ticks as in tick boxes.
    [[nodiscard]] inline MenuStyle menu_style()
    {
        MenuStyle s;
        s.background = kMenuBackground;
        s.highlight_skin = kSelectedRow;
        s.tick = kTick;
        s.font = FontRole::Small;
        s.padding = { 6, 6, 9, 9 }; // The surround's shadow takes the right and bottom
        s.tick_room = 20;
        return s;
    }

    /// A drop-down in the edit box frame with the game's opener arrow.
    [[nodiscard]] inline DropdownStyle dropdown_style()
    {
        DropdownStyle s;
        s.field = kListBox;
        s.focused = kListBoxFocused;
        s.opener = kDropdownOpener;
        s.opener_width = 15;
        s.menu = menu_style();
        return s;
    }

    /// A menu bar on the window background, the open title on the blue patch.
    [[nodiscard]] inline MenuBarStyle menu_bar_style()
    {
        MenuBarStyle s;
        s.background = kWindowBackground;
        s.highlight_skin = kSelectedRow;
        s.menu = menu_style();
        return s;
    }

    /// Square buttons on the window background, swelling under the pointer (our measure).
    [[nodiscard]] inline ToolbarStyle toolbar_style()
    {
        ToolbarStyle s;
        s.background = kWindowBackground;
        s.button = kButton;
        s.button_size = 30;
        s.swell = 2;
        return s;
    }

    /// A question in a window, its answers as text lozenges.
    [[nodiscard]] inline QuestionBoxStyle question_box_style()
    {
        QuestionBoxStyle s;
        s.window = window_style();
        s.button = kTextButton;
        s.button_inset = kLozengeInset;
        s.width = 320;
        s.height = 180;
        return s;
    }

    /// The options window: tick boxes, lists, sliders and number fields as everywhere else, in rows 18 units
    /// high (ours, to fit the small font).
    [[nodiscard]] inline OptionsDialogStyle options_dialog_style()
    {
        OptionsDialogStyle s;
        s.window = window_style();
        s.rows.tick_box = kTickbox;
        s.rows.tick = kTick;
        s.rows.dropdown = dropdown_style();
        s.rows.slider = slider_style();
        s.rows.spinner = spinner_style();
        s.rows.key_box = kListBox;
        s.rows.key_box_focused = kListBoxFocused;
        s.scroll = scroll_bar_style(Orientation::Vertical);
        return s;
    }

    /// The game's tooltip: its skin and small font after half a second of rest, with four units between
    /// the text and the inside of the lozenge.
    [[nodiscard]] inline TooltipStyle tooltip_style()
    {
        TooltipStyle s;
        s.skin = kTooltip;
        s.font = FontRole::Small;
        s.delay = 0.5f;
        s.padding = { kLozengeInset.x0 + 4, kLozengeInset.y0 + 4, kLozengeInset.x1 + 4, kLozengeInset.y1 + 4 };
        return s;
    }

} // namespace rct3::ui::game_skins
