// SPDX-License-Identifier: GPL-3.0-or-later
//
// The skins the game's own interface is drawn with, by the names its data uses, and the measurements that
// go with them. The measurements are ours, chosen to fit the skins and to match the game's dialogs as they
// show on screen.
#pragma once

#include "ui/font_set.hpp"
#include "ui/screens/career_board.hpp"
#include "ui/screens/career_dialog.hpp"
#include "ui/screens/file_dialog.hpp"
#include "ui/screens/options_dialog.hpp"
#include "ui/screens/park_bar.hpp"
#include "ui/screens/tutorial_picker.hpp"
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

#include <array>
#include <string_view>
#include <utility>

namespace rct3::ui::game_skins
{

    inline constexpr std::string_view kWindowFrame = "GUI2Dialog_Frame";
    inline constexpr std::string_view kWindowFrameUnfocused = "GUI2Dialog_FrameNoFocus";
    inline constexpr std::string_view kWindowBackground = "GUI2Dialog_Background"; ///< A small tile, repeated
    inline constexpr std::string_view kMiniBackground = "GUI2MiniDialog_Background";
    inline constexpr std::string_view kMiniFrame = "GUI2MiniDialog_Frame";
    /// Textures drawn whole rather than cut by a skin; loaded as skins of their own name.
    inline constexpr std::array<std::string_view, 1> kWholeTextures = { kWindowBackground };
    inline constexpr std::string_view kTitleButton = "GUI2Dialog_TitleButton"; ///< Family
    inline constexpr std::string_view kCloseIcon = "GUI2Dialog_CloseIcon";
    inline constexpr std::string_view kButton = "GUI2Button";
    inline constexpr std::string_view kConfirmIcon = "GUI2MiniDialog_Confirm";
    inline constexpr std::string_view kCancelIcon = "GUI2MiniDialog_Cancel";
    inline constexpr std::string_view kLoadIcon = "GUI2_Load";
    inline constexpr std::string_view kSaveIcon = "GUI2_Save";
    inline constexpr std::string_view kFileCancelIcon = "GUI2LoadSave_Cancel";
    inline constexpr std::string_view kFileDeleteIcon = "GUI2LoadSave_DeleteDat";
    inline constexpr std::string_view kStandardBorder = "GUI2StandardBorder";
    inline constexpr std::string_view kCareerIcon = "Objective_Cup";
    inline constexpr std::string_view kNewCareerIcon = "GUI2_StartNewScenario";
    inline constexpr std::string_view kTycoonHat = "Difficulty_Hat_Tycoon";
    inline constexpr std::string_view kEntrepreneurHat = "Difficulty_Hat_Entrepreneur";
    inline constexpr std::string_view kApprenticeHat = "Difficulty_Hat_Apprentice";
    inline constexpr std::string_view kNoParkPicture = "GUI2TestDat";
    // The career map.
    inline constexpr std::string_view kMenuCircle = "FrontScreen_IconCircle_New";
    inline constexpr std::string_view kMenuButtonSmall = "MenuButtonSmall";
    inline constexpr std::string_view kResumeIcon = "GUI2_ResumeLastSaved";
    inline constexpr std::string_view kMainMenuIcon = "GUI2_QuitToFrontScreen";
    inline constexpr std::string_view kQuitIcon = "File_Quit";
    inline constexpr std::string_view kPadlockLocked = "GUI2PadlockLocked";
    inline constexpr std::string_view kPadlockOpen = "GUI2PadlockUnlocked";
    inline constexpr std::string_view kNoScenarioPicture = "FrontScreen_PeopleUT";
    inline constexpr std::string_view kStartTutorialIcon = "GUI2_TutorialForward";
    inline constexpr std::array<std::string_view, 4> kQuarterCircles = { "FrontScreen_QuarterCircle_TL", "FrontScreen_QuarterCircle_TR", "FrontScreen_QuarterCircle_BL", "FrontScreen_QuarterCircle_BR" };
    inline constexpr std::string_view kTickbox = "Tickbox_Up";
    inline constexpr std::string_view kTick = "Tickbox_Tick";
    inline constexpr std::string_view kListBox = "EditBox_NoFocus";
    inline constexpr std::string_view kListBoxFocused = "EditBox_Focus";
    inline constexpr std::string_view kSelectedRow = "GUI2SolidBlueBackground";
    inline constexpr std::string_view kTooltip = "GUI2Swatch_Background";
    inline constexpr std::string_view kMenuBackground = "GUIMenuSurround";
    inline constexpr std::string_view kDropdownOpener = "ListBoxOpener"; ///< Family: "ListBoxOpenerDisabled"
    inline constexpr std::string_view kSliderThumb = "LeftRight_SliderArrows";
    inline constexpr std::string_view kOptionsIcon = "File_GameOptions";
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
        s.tile_background = true;
        s.background_inset = { 1, 10, 4, 4 };
        s.title_button = kTitleButton;
        s.close_icon = kCloseIcon;
        // The frame's top-left corner holds the window's icon in its circle and the title rule; content goes
        // below the rule. The frame's right part is thicker than its left.
        s.content_inset = { 8, 49, 13, 13 };
        s.icon_box = { 6, 17, 52, 63 };
        s.title_button_margin = { 8, 2 }; // On the frame's top band, as the game places it
        s.title_font = FontRole::Large;
        s.title_box = { 67, 18, 13, 42 }; // Right of the icon, above the title rule
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
        s.rail_colour = hex("#1464a0");
        s.rail_thickness = 4;
        s.thumb_width = 18;
        s.thumb_height = 13;
        return s;
    }

    /// The game's level bar: light blue as far as the value, grey after, no thumb.
    [[nodiscard]] inline SliderStyle level_bar_style()
    {
        SliderStyle s;
        s.bar = true;
        s.thumb_width = 0;
        s.thumb_height = 0;
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

    /// A drop-down as the game draws it closed: plain white, the opener arrow in its top-right corner.
    [[nodiscard]] inline DropdownStyle dropdown_style()
    {
        DropdownStyle s;
        s.fill = { 255, 255, 255, 255 };
        s.opener = kDropdownOpener;
        s.opener_width = 15;
        s.opener_in_corner = true;
        s.text_indent = 4;
        s.menu = menu_style();
        return s;
    }

    /// A menu bar on the window background, the open title on the blue patch.
    [[nodiscard]] inline MenuBarStyle menu_bar_style()
    {
        MenuBarStyle s;
        s.background = kMiniBackground;
        s.highlight_skin = kSelectedRow;
        s.menu = menu_style();
        return s;
    }

    /// Square buttons on the window background, swelling under the pointer (our measure).
    [[nodiscard]] inline ToolbarStyle toolbar_style()
    {
        ToolbarStyle s;
        s.background = kMiniBackground;
        s.button = kButton;
        s.button_size = 30;
        s.swell = 2;
        return s;
    }

    /// The small window of messages and questions: a plain blue band on top with the close button, no icon
    /// and no title, the content on a white panel.
    [[nodiscard]] inline WindowStyle mini_window_style()
    {
        WindowStyle s;
        s.frame = kMiniFrame;
        s.frame_unfocused = kMiniFrame;
        s.background = kWindowBackground;
        s.tile_background = true;
        s.background_inset = { 1, 10, 4, 4 };
        s.content_background = kMiniBackground;
        s.title_button = kTitleButton;
        s.close_icon = kCloseIcon;
        s.title_height = 18;
        s.title_button_margin = { 8, 2 };
        s.content_inset = { 12, 28, 15, 14 };
        return s;
    }

    /// A question in the small window, its answers as round buttons with a tick or a cross.
    [[nodiscard]] inline QuestionBoxStyle question_box_style()
    {
        QuestionBoxStyle s;
        s.window = mini_window_style();
        s.button = kButton;
        s.button_inset = kLozengeInset;
        s.confirm_icon = kConfirmIcon;
        s.cancel_icon = kCancelIcon;
        return s;
    }

    /// The options window as the game lays it out: rows as tall as the small font with a little between,
    /// 12 unit tick boxes, level bars, the page's name in blue left of the tabs.
    [[nodiscard]] inline OptionsDialogStyle options_dialog_style()
    {
        OptionsDialogStyle s;
        s.window = window_style();
        s.icon = kOptionsIcon;
        s.rows.tick_box = kTickbox;
        s.rows.tick = kTick;
        s.rows.tick_size = 12;
        s.rows.tick_gap = 5;
        s.rows.row_height = 15;
        s.rows.row_gap = 2;
        s.rows.section_gap = 6;
        s.rows.dropdown = dropdown_style();
        s.rows.slider = level_bar_style();
        s.rows.slider_inset = 2;
        s.rows.spinner = spinner_style();
        s.rows.key_box = kListBox;
        s.rows.key_box_focused = kListBoxFocused;
        s.keys.focused = kListBoxFocused; // A blue outline
        s.scroll = scroll_bar_style(Orientation::Vertical);
        s.button = kTextButton;
        s.button_inset = kLozengeInset;
        return s;
    }

    /// The load and save window in the game's frame, the list and pictures in its plain border.
    [[nodiscard]] inline FileDialogStyle file_dialog_style()
    {
        FileDialogStyle s;
        s.window = window_style();
        s.question = question_box_style();
        s.scroll = scroll_bar_style(Orientation::Vertical);
        s.load_icon = kLoadIcon;
        s.save_icon = kSaveIcon;
        s.cancel_icon = kFileCancelIcon;
        s.delete_icon = kFileDeleteIcon;
        s.button = kButton;
        s.border = kStandardBorder;
        s.name_box = kListBox;
        s.name_box_focused = kListBoxFocused;
        return s;
    }

    /// The window that picks a career in the game's frame, its rows framed like edit boxes.
    [[nodiscard]] inline CareerDialogStyle career_dialog_style()
    {
        CareerDialogStyle s;
        s.window = window_style();
        s.question = question_box_style();
        s.scroll = scroll_bar_style(Orientation::Vertical);
        s.icon = kCareerIcon;
        s.button = kButton;
        s.new_icon = kNewCareerIcon;
        s.load_icon = kLoadIcon;
        s.cancel_icon = kFileCancelIcon;
        s.delete_icon = kFileDeleteIcon;
        s.hats = { std::string(kTycoonHat), std::string(kEntrepreneurHat), std::string(kApprenticeHat) };
        s.row_frame = kListBox;
        s.row_frame_selected = kListBoxFocused;
        s.no_picture = kNoParkPicture;
        s.name_box_focused = kListBoxFocused;
        return s;
    }

    /// The career map with the front screen's buttons and the game's marks.
    [[nodiscard]] inline CareerBoardStyle career_board_style()
    {
        CareerBoardStyle s;
        s.question = question_box_style();
        s.scenario_button = kMenuCircle;
        s.small_button = kMenuButtonSmall;
        s.panel = kTooltip;
        s.new_icon = kNewCareerIcon;
        s.resume_icon = kResumeIcon;
        s.load_icon = kLoadIcon;
        s.menu_icon = kMainMenuIcon;
        s.quit_icon = kQuitIcon;
        s.locked = kPadlockLocked;
        s.unlocked = kPadlockOpen;
        s.hats = { std::string(kApprenticeHat), std::string(kEntrepreneurHat), std::string(kTycoonHat) };
        s.no_picture = kNoScenarioPicture;
        return s;
    }

    /// The tutorials' menu on the front screen's buttons and white disc.
    [[nodiscard]] inline TutorialPickerStyle tutorial_picker_style()
    {
        TutorialPickerStyle s;
        s.button = kMenuCircle;
        s.start_icon = kStartTutorialIcon;
        for (std::size_t i = 0; i < kQuarterCircles.size(); ++i)
            s.quarters.at(i) = kQuarterCircles[i];
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

    /// The bar along the top of the park view.
    [[nodiscard]] inline ParkBarStyle park_bar_style()
    {
        ParkBarStyle s;
        s.left_frame = "Top_WindowFrame_Left";
        s.divider = "Top_WindowFrame_Middle";
        s.right_frame = "Top_WindowFrame_Right";
        s.objective_icon = kApprenticeHat;
        s.speed_icons = { "Top_Pause", "Top_Play", "Top_FastForward", "Top_SuperFastForward" };
        s.picked_speed_icons = { "Top_PauseSelected", "Top_PlaySelected", "Top_FastForwardSelected", "Top_SuperFastForwardSelected" };
        s.thumb_down = "Top_ThumbsDown";
        s.thumb_up = "Top_ThumbsUp";
        s.weather_icons = { "IconSun", "IconCloudWithSun", "IconCloud", "IconLightRain", "IconHeavyRain", "IconThunderstorm", "IconSnow" };
        s.forecast_arrow = "IconWeatherArrow";
        return s;
    }

} // namespace rct3::ui::game_skins
