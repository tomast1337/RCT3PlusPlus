// SPDX-License-Identifier: GPL-3.0-or-later
//
// The skins the game's own interface is drawn with, by the names its data uses, and the measurements that
// go with them. The measurements are ours, chosen to fit the skins and to match the game's dialogs as they
// show on screen. Every style, and every frame drawn in code, takes its colours from a palette (palette.hpp).
#pragma once

#include "ui/font_set.hpp"
#include "ui/game_frames.hpp"
#include "ui/palette.hpp"
#include "ui/panel_look.hpp"
#include "ui/screens/about_window.hpp"
#include "ui/screens/build_picker.hpp"
#include "ui/screens/career_board.hpp"
#include "ui/screens/career_dialog.hpp"
#include "ui/screens/construction_panel.hpp"
#include "ui/screens/file_dialog.hpp"
#include "ui/screens/footpath_panel.hpp"
#include "ui/screens/front_screen.hpp"
#include "ui/screens/info_panel.hpp"
#include "ui/screens/land_panel.hpp"
#include "ui/screens/management_window.hpp"
#include "ui/screens/message_drawer.hpp"
#include "ui/screens/options_dialog.hpp"
#include "ui/screens/park_bar.hpp"
#include "ui/screens/ride_camera_panel.hpp"
#include "ui/screens/tool_row.hpp"
#include "ui/screens/tutorial_guide.hpp"
#include "ui/screens/tutorial_picker.hpp"
#include "ui/skin_library.hpp"
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

#include <algorithm>
#include <array>
#include <format>
#include <string_view>
#include <utility>
#include <vector>

namespace rct3::ui::game_skins
{

    /// The overlay in the install, relative to its root, that holds the interface's skins.
    inline constexpr std::string_view kMainOverlay = "Main.common.ovl";

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
    /// The guide with the mortarboard; the front screen's tutorials button shows it too.
    inline constexpr std::string_view kTutorialGuide = "FrontScreen_Icon_Tutorial";
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
    inline constexpr std::string_view kAutoDetectIcon = "GameOptionsDialog_AutoConfigIcon";
    inline constexpr std::string_view kAboutIcon = "File_AboutRCT";
    inline constexpr std::string_view kCreditsIcon = "File_Credits_Stars";
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
    inline constexpr std::string_view kDrawerLipLeft = "MessageConsole_Left";
    inline constexpr std::string_view kDrawerLipRight = "MessageConsole_Right";
    inline constexpr std::string_view kDrawerBack = "MessageConsole_Top";
    // The lists of things to build.
    inline constexpr std::string_view kObjectFilter = "ObjectFilter_Colour_";         ///< + a kind's name
    inline constexpr std::string_view kObjectFilterOff = "ObjectFilter_Grey_";        ///< + a kind's name
    inline constexpr std::string_view kObjectFilterHelp = "ObjectFilter_TT_";         ///< + a kind's name
    inline constexpr std::string_view kStyleFilter = "StyleFilter_";                  ///< + a theme's name
    inline constexpr std::string_view kStyleFilterOff = "StyleFilter_Grey_";          ///< + a theme's name
    inline constexpr std::string_view kPackBadge = "ObjectSelectionWindow_AOPBadge_"; ///< + a pack's number from 0
    inline constexpr std::string_view kPackName = "AOP_Name_";                        ///< + a pack's number from 0
    inline constexpr std::string_view kStyleFilterHelp = "ASM_StyleFilterHelp";
    inline constexpr std::string_view kOpenHeading = "Box_Minus";
    inline constexpr std::string_view kClosedHeading = "Box_Plus";
    inline constexpr std::string_view kCurvedStrip = "BackgroundCurvedMirrored";

    /// A skin that can be drawn in code (game_frames.hpp), the panel it belongs to and whether the game's look
    /// draws it so. Panels move to code one at a time: tools/panel-look scores each against the pictures, and
    /// one that does not yet read as the same stays on them.
    struct CodedSkin
    {
        std::string_view skin;
        std::string_view panel;
        PanelLook look = PanelLook::Texture;
        CodedFrame frame;
    };

    /// The front screen's design, the rectangle its backdrop's pieces are cut from.
    inline constexpr Rect kFrontDesign { 0, 0, front_screen::kDesignWidth, front_screen::kDesignHeight };

    [[nodiscard]] inline std::vector<CodedSkin> coded_skins(const Palette& p)
    {
        using enum PanelLook;
        using game_frames::Arrow;
        using game_frames::Quarter;
        return {
            { kTextButton, "lozenge", Coded, game_frames::lozenge(p) },
            { "GUI2Swatch_BackgroundDisabled", "lozenge", Coded, game_frames::lozenge(p, false) },
            { kMenuBackground, "menu", Coded, game_frames::lozenge(p) },
            { kSelectedRow, "selected-row", Coded, game_frames::selected_row(p) },
            { kMenuButtonSmall, "button-face", Coded, game_frames::button_face(p) },
            { kMenuCircle, "menu-circle", Coded, game_frames::menu_circle(p) },
            { kQuarterCircles[0], "tutorial-disc", Coded, game_frames::quarter_circle(p, Quarter::TopLeft) },
            { kQuarterCircles[1], "tutorial-disc", Coded, game_frames::quarter_circle(p, Quarter::TopRight) },
            { kQuarterCircles[2], "tutorial-disc", Coded, game_frames::quarter_circle(p, Quarter::BottomLeft) },
            { kQuarterCircles[3], "tutorial-disc", Coded, game_frames::quarter_circle(p, Quarter::BottomRight) },
            { kDrawerBack, "message-drawer", Coded, game_frames::drawer_back(p) },
            { kDrawerLipLeft, "message-drawer", Coded, game_frames::drawer_lip_left(p) },
            { kDrawerLipRight, "message-drawer", Coded, game_frames::drawer_lip_right(p) },
            { kMiniFrame, "mini-window", Coded, game_frames::mini_window(p) },
            { kMiniBackground, "mini-window", Coded, game_frames::mini_window_panel(p) },
            { kCurvedStrip, "construction-strip", Coded, game_frames::curved_strip(p) },
            { kWindowFrame, "window", Coded, game_frames::window(p) },
            { kWindowFrameUnfocused, "window", Coded, game_frames::window(p) },
            { kWindowBackground, "window-background", Coded, game_frames::window_background(p) },
            { front_screen::kBandTopSkin, "front-backdrop", Coded, game_frames::front_piece(game_frames::front_backdrop(p, kFrontDesign), front_screen::kBandTop) },
            { front_screen::kBandBottomSkin, "front-backdrop", Coded, game_frames::front_piece(game_frames::front_backdrop(p, kFrontDesign), front_screen::kBandBottom) },
            { front_screen::kStripRightSkin, "front-backdrop", Coded, game_frames::front_piece(game_frames::front_backdrop(p, kFrontDesign), front_screen::kStripRight) },
            { front_screen::kPatchSkin, "label-band", Coded, game_frames::label_patch(p) },
            { front_screen::kBandEndSkin, "label-band", Coded, game_frames::label_end(p) },
            { kStandardBorder, "controls", Coded, game_frames::plain_box(p) },
            { kListBox, "controls", Coded, game_frames::plain_box(p) },
            { kListBoxFocused, "controls", Coded, game_frames::focused_box(p) },
            { kButton, "controls", Coded, game_frames::round_button(p) },
            { kTickbox, "controls", Coded, game_frames::tickbox(p) },
            { kVerticalRail, "controls", Coded, game_frames::scroll_rail(p) },
            { kHorizontalRail, "controls", Coded, game_frames::scroll_rail(p) },
            { "VScrollBar_RiderUp", "controls", Coded, game_frames::scroll_rider(p) },
            { "VScrollBar_RiderUpOver", "controls", Coded, game_frames::scroll_rider(p) },
            { "VScrollBar_RiderPressed", "controls", Coded, game_frames::scroll_rider(p) },
            { "HScrollBar_RiderUp", "controls", Coded, game_frames::scroll_rider(p) },
            { "HScrollBar_RiderUpOver", "controls", Coded, game_frames::scroll_rider(p) },
            { "HScrollBar_RiderPressed", "controls", Coded, game_frames::scroll_rider(p) },
            { kScrollUp, "controls", Coded, game_frames::scroll_arrow(p, Arrow::Up) },
            { kScrollDown, "controls", Coded, game_frames::scroll_arrow(p, Arrow::Down) },
            { kScrollLeft, "controls", Coded, game_frames::scroll_arrow(p, Arrow::Left) },
            { kScrollRight, "controls", Coded, game_frames::scroll_arrow(p, Arrow::Right) },
            { kDropdownOpener, "controls", Coded, game_frames::square_arrow(p, Arrow::Down, true) },
            { "ListBoxOpenerDisabled", "controls", Coded, game_frames::opener_disabled(p) },
            { "SquareArrowUp", "controls", Coded, game_frames::square_arrow(p, Arrow::Up, true) },
            { "SquareArrowDown", "controls", Coded, game_frames::square_arrow(p, Arrow::Down, true) },
            { "SquareArrowLeft", "controls", Coded, game_frames::square_arrow(p, Arrow::Left, true) },
            { "SquareArrowRight", "controls", Coded, game_frames::square_arrow(p, Arrow::Right, true) },
            { "SquareArrowUpDisabled", "controls", Coded, game_frames::square_arrow(p, Arrow::Up, false) },
            { "SquareArrowDownDisabled", "controls", Coded, game_frames::square_arrow(p, Arrow::Down, false) },
            { kSpinnerUp, "controls", Coded, game_frames::spinner(p, Arrow::Up, true) },
            { kSpinnerDown, "controls", Coded, game_frames::spinner(p, Arrow::Down, true) },
            { "SpinnerUpDisabled", "controls", Coded, game_frames::spinner(p, Arrow::Up, false) },
            { "SpinnerDownDisabled", "controls", Coded, game_frames::spinner(p, Arrow::Down, false) },
            { "GUI2Dialog_TitleButtonUp", "title-button", Coded, game_frames::title_button(p) },
            { "GUI2Dialog_TitleButtonUpOver", "title-button", Coded, game_frames::title_button(p) },
            { "GUI2Dialog_TitleButtonPressed", "title-button", Coded, game_frames::title_button(p) },
            { kOpenHeading, "small-parts", Coded, game_frames::heading_box(p, false) },
            { kClosedHeading, "small-parts", Coded, game_frames::heading_box(p, true) },
            { kSliderThumb, "small-parts", Coded, game_frames::slider_thumb(p) },
            { "FlexiColourHighlightBlue", "small-parts", Coded, game_frames::colour_ring(p.colour_ring, 18) },
            { "FlexiColourHighlightWhite", "small-parts", Coded, game_frames::colour_ring(p.colour_current, 20) },
            { "FlexiColourSurround", "small-parts", Coded, game_frames::colour_back(p) },
            { "Top_WindowFrame_Left", "park-bar", Coded, game_frames::park_bar_left(p) },
            { "Top_WindowFrame_Middle", "park-bar", Coded, game_frames::park_bar_middle(p, park_bar::kDividerWidth, park_bar::kHeight, park_bar::kNotchRadius) },
            { "Top_WindowFrame_Right", "park-bar", Coded, game_frames::park_bar_right(p) },
        };
    }

    /// Whether the game's look draws the front screen's backdrop in code, and so lays the screen out at one
    /// scale (front_screen_layout.hpp).
    [[nodiscard]] inline bool front_screen_round()
    {
        return std::ranges::any_of(coded_skins(Palette { }), [](const CodedSkin& c) { return c.panel == "front-backdrop" && c.look == PanelLook::Coded; });
    }

    /// Has `skins` draw in code, in the colours of `p`, every skin whose panel the game's look has moved to
    /// code. Called again with another palette, it recolours them.
    inline void use_coded_skins(SkinLibrary& skins, const Palette& p)
    {
        for (CodedSkin& coded : coded_skins(p))
        {
            if (coded.look == PanelLook::Coded)
                skins.set_coded(coded.skin, std::move(coded.frame));
        }
    }

    [[nodiscard]] inline WindowStyle window_style(const Palette& p)
    {
        WindowStyle s;
        s.title_colour = p.text;
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
    [[nodiscard]] inline ScrollBarStyle scroll_bar_style(const Palette&, Orientation orientation)
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
    [[nodiscard]] inline ListBoxStyle list_box_style(const Palette& p)
    {
        ListBoxStyle s;
        s.selection_colour = p.selection;
        s.text_colour = p.text;
        s.disabled_text_colour = p.text_disabled;
        s.background = kListBox;
        s.focused = kListBoxFocused;
        s.selection_skin = kSelectedRow;
        s.font = FontRole::Small;
        s.row_height = 18;
        return s;
    }

    /// Tabs as text lozenges, the current one drawn down. The game's tab strips are rows of text buttons;
    /// the sizes are ours.
    [[nodiscard]] inline TabsStyle tabs_style(const Palette&)
    {
        TabsStyle s;
        s.tab = kTextButton;
        s.text_inset = kLozengeInset;
        s.font = FontRole::Small;
        s.bar_height = 24;
        return s;
    }

    /// The green double arrow at the size the skin has, on a line in the frames' blue (ours).
    [[nodiscard]] inline SliderStyle slider_style(const Palette&)
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
    [[nodiscard]] inline SliderStyle level_bar_style(const Palette& p)
    {
        SliderStyle s;
        s.bar_colour = p.icon;
        s.bar_back_colour = p.text_disabled;
        s.bar = true;
        s.thumb_width = 0;
        s.thumb_height = 0;
        return s;
    }

    /// The number in the edit box frame, the two arrows stacked at their size.
    [[nodiscard]] inline SpinnerStyle spinner_style(const Palette&)
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
    [[nodiscard]] inline MenuStyle menu_style(const Palette& p)
    {
        MenuStyle s;
        s.highlight_colour = p.selection;
        s.text_colour = p.text;
        s.disabled_text_colour = p.text_disabled;
        s.background = kMenuBackground;
        s.highlight_skin = kSelectedRow;
        s.tick = kTick;
        s.font = FontRole::Small;
        s.padding = { 6, 6, 9, 9 }; // The surround's shadow takes the right and bottom
        s.tick_room = 20;
        return s;
    }

    /// A drop-down as the game draws it closed: plain white, the opener arrow in its top-right corner.
    [[nodiscard]] inline DropdownStyle dropdown_style(const Palette& p)
    {
        DropdownStyle s;
        s.text_colour = p.text;
        s.disabled_text_colour = p.text_disabled;
        s.fill = p.paper;
        s.opener = kDropdownOpener;
        s.opener_width = 15;
        s.opener_in_corner = true;
        s.text_indent = 4;
        s.menu = menu_style(p);
        return s;
    }

    /// A menu bar on the window background, the open title on the blue patch.
    [[nodiscard]] inline MenuBarStyle menu_bar_style(const Palette& p)
    {
        MenuBarStyle s;
        s.highlight_colour = p.selection;
        s.text_colour = p.text;
        s.background = kMiniBackground;
        s.highlight_skin = kSelectedRow;
        s.menu = menu_style(p);
        return s;
    }

    /// Square buttons on the window background, swelling under the pointer (our measure).
    [[nodiscard]] inline ToolbarStyle toolbar_style(const Palette&)
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
    [[nodiscard]] inline WindowStyle mini_window_style(const Palette& p)
    {
        WindowStyle s;
        s.title_colour = p.text;
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
    [[nodiscard]] inline QuestionBoxStyle question_box_style(const Palette& p)
    {
        QuestionBoxStyle s;
        s.text_colour = p.text;
        s.hover_colour = p.accent;
        s.window = mini_window_style(p);
        s.button = kButton;
        s.button_inset = kLozengeInset;
        s.confirm_icon = kConfirmIcon;
        s.cancel_icon = kCancelIcon;
        return s;
    }

    /// The options window as the game lays it out: rows as tall as the small font with a little between,
    /// 12 unit tick boxes, level bars, the page's name in blue left of the tabs.
    [[nodiscard]] inline OptionsDialogStyle options_dialog_style(const Palette& p)
    {
        OptionsDialogStyle s;
        s.caption_colour = p.accent;
        s.keys.background = p.paper;
        s.rows.text_colour = p.text;
        s.rows.disabled_colour = p.text_disabled;
        s.window = window_style(p);
        s.icon = kOptionsIcon;
        s.rows.tick_box = kTickbox;
        s.rows.tick = kTick;
        s.rows.tick_size = 12;
        s.rows.tick_gap = 5;
        s.rows.row_height = 15;
        s.rows.row_gap = 2;
        s.rows.section_gap = 6;
        s.rows.dropdown = dropdown_style(p);
        s.rows.slider = level_bar_style(p);
        s.rows.slider_inset = 2;
        s.rows.spinner = spinner_style(p);
        s.rows.key_box = kListBox;
        s.rows.key_box_focused = kListBoxFocused;
        s.keys.focused = kListBoxFocused; // A blue outline
        s.scroll = scroll_bar_style(p, Orientation::Vertical);
        s.button = kTextButton;
        s.button_inset = kLozengeInset;
        s.auto_detect_icon = kAutoDetectIcon;
        return s;
    }

    /// The about window in the game's frame: the page's name in blue left of two small picture tabs.
    [[nodiscard]] inline AboutWindowStyle about_window_style(const Palette& p)
    {
        AboutWindowStyle s;
        s.caption_colour = p.accent;
        s.text_colour = p.text;
        s.role_colour = p.accent;
        s.window = window_style(p);
        s.icon = kAboutIcon;
        s.credits_icon = kCreditsIcon;
        return s;
    }

    /// The park's management windows in the large frame, laid out as the options window: picture tabs in the
    /// top-right corner, the open tab's name in blue on their left. Their lists sit in the edit box frame
    /// under a band of headings in the frames' blue (ours).
    [[nodiscard]] inline ManagementWindowStyle management_window_style(const Palette& p)
    {
        ManagementWindowStyle s;
        s.caption_colour = p.accent;
        s.text_colour = p.text;
        s.heading_colour = p.accent;
        s.window = window_style(p);
        s.scroll = scroll_bar_style(p, Orientation::Vertical);
        s.table.background = kListBox;
        s.table.selection_skin = kSelectedRow;
        s.button = kTextButton;
        s.button_inset = kLozengeInset;
        return s;
    }

    /// The load and save window in the game's frame, the list and pictures in its plain border.
    [[nodiscard]] inline FileDialogStyle file_dialog_style(const Palette& p)
    {
        FileDialogStyle s;
        s.text_colour = p.text;
        s.line_colour = p.accent;
        s.selected_colour = p.stripe;
        s.faded_colour = p.paper;
        s.window = window_style(p);
        s.question = question_box_style(p);
        s.scroll = scroll_bar_style(p, Orientation::Vertical);
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
    [[nodiscard]] inline CareerDialogStyle career_dialog_style(const Palette& p)
    {
        CareerDialogStyle s;
        s.text_colour = p.text;
        s.hover_colour = p.accent;
        s.row_colours = { p.stripe, p.paper };
        s.window = window_style(p);
        s.question = question_box_style(p);
        s.scroll = scroll_bar_style(p, Orientation::Vertical);
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
    [[nodiscard]] inline CareerBoardStyle career_board_style(const Palette& p)
    {
        CareerBoardStyle s;
        s.text_colour = p.text;
        s.question = question_box_style(p);
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
    [[nodiscard]] inline TutorialPickerStyle tutorial_picker_style(const Palette& p)
    {
        TutorialPickerStyle s;
        s.text_colour = p.text;
        s.button = kMenuCircle;
        s.start_icon = kStartTutorialIcon;
        for (std::size_t i = 0; i < kQuarterCircles.size(); ++i)
            s.quarters.at(i) = kQuarterCircles[i];
        return s;
    }

    /// The tutorial guide in the large frame: the guide in the corner circle, the step's title on a line under
    /// the title rule, the page below that.
    [[nodiscard]] inline TutorialGuideStyle tutorial_guide_style(const Palette& p)
    {
        TutorialGuideStyle s;
        s.text_colour = p.text;
        s.window = window_style(p);
        s.window.content_inset.y0 += s.step_line.y1;
        s.portrait = kTutorialGuide;
        s.button = kTextButton;
        s.button_inset = kLozengeInset;
        return s;
    }

    /// The game's tooltip: its skin and small font after half a second of rest, with four units between
    /// the text and the inside of the lozenge.
    [[nodiscard]] inline TooltipStyle tooltip_style(const Palette& p)
    {
        TooltipStyle s;
        s.colour = p.text;
        s.skin = kTooltip;
        s.font = FontRole::Small;
        s.delay = 0.5f;
        s.padding = { kLozengeInset.x0 + 4, kLozengeInset.y0 + 4, kLozengeInset.x1 + 4, kLozengeInset.y1 + 4 };
        return s;
    }

    /// The bar along the top of the park view.
    [[nodiscard]] inline ParkBarStyle park_bar_style(const Palette& p)
    {
        ParkBarStyle s;
        s.text_colour = p.text_on_band;
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

    /// The panel shown while riding along in a car.
    [[nodiscard]] inline RideCameraPanelStyle ride_camera_panel_style(const Palette& p)
    {
        RideCameraPanelStyle s;
        s.text_colour = p.text_on_band;
        s.back = "CircularPanelDialog";
        s.button = kMenuButtonSmall;
        s.pin = "RideCamGUI_Pin";
        s.unpin = "RideCamGUI_UnPin";
        s.leave = "RideCamGUI_Exit";
        s.types = { "RideCamGUI_Nose", "RideCamGUI_Peep", "RideCamGUI_Rear", "RideCamGUI_FlatPeep", "RideCamGUI_FlatStatic" };
        s.steps = { "RideCamGUI_PrvCam", "RideCamGUI_NxtCam", "RideCamGUI_PrvRide", "RideCamGUI_NxtRide" };
        s.no_steps = { "RideCamGUI_PrvCamDis", "RideCamGUI_NxtCamDis", "RideCamGUI_PrvRideDis", "RideCamGUI_NxtRideDis" };
        return s;
    }

    /// The park's messages, hanging from the middle of the bar.
    [[nodiscard]] inline MessageDrawerStyle message_drawer_style(const Palette& p)
    {
        MessageDrawerStyle s;
        s.text_colour = p.text;
        s.stripe = p.paper;
        s.lip_left = kDrawerLipLeft;
        s.lip_right = kDrawerLipRight;
        s.back = kDrawerBack;
        s.older = "SquareArrowUp";
        s.newer = "SquareArrowDown";
        s.locate = "Icon_PointCameraAt";
        return s;
    }

    /// The round panel in the bottom-right corner that tells about an attraction. Its frame is drawn in code,
    /// which panel-look scores against these skins; the skins stay named for falling back to them.
    [[nodiscard]] inline InfoPanelStyle info_panel_style(const Palette& p)
    {
        InfoPanelStyle s;
        s.coded.body = p.strip;
        s.coded.band = p.accent;
        s.coded.disc = p.patch;
        s.coded.paper = p.paper;
        s.coded.stripe = p.stripe;
        s.coded.margin = p.paper_line;
        s.paper_text_colour = p.text;
        s.hide_icon = "WhiteArrow_Right";
        s.hide_icon_left = "WhiteArrow_Left";
        s.picked_section_colour = p.text_on_band;
        s.text_colour = p.text_on_band;
        s.look = PanelLook::Coded;
        s.disc = "CircularViewSolid";
        s.glass = "CircularViewHollow";
        s.rim = "CircularViewHollowEdgeR";
        s.shadow = "CircularViewShadow";
        s.rim_shadow = "CircularViewHollowShadowR";
        s.strip = "CircularPanelDialog";
        s.close_icon = kCloseIcon;
        s.tab_face = kMenuButtonSmall;
        s.paint.pots = { "FlexiColourOnePaintPot", "FlexiColourTwoPaintPots", "FlexiColourThreePaintPots" };
        s.paint.lit = { {
            { "FlexiColourOnePaintPotH0", { }, { } },
            { "FlexiColourTwoPaintPotsH0", "FlexiColourTwoPaintPotsH1", { } },
            { "FlexiColourThreePaintPotsH0", "FlexiColourThreePaintPotsH1", "FlexiColourThreePaintPotsH2" },
        } };
        // Measured on the pot pictures: where each pot's mouth is, in the order the lit pictures light them (top
        // first when there are three).
        s.paint.spots = { {
            { { { 20, 22 }, 18 } },
            { { { 13, 22 }, 14 }, { { 28, 22 }, 14 } },
            { { { 20, 14 }, 11 }, { { 13, 27 }, 11 }, { { 27, 27 }, 11 } },
        } };
        s.paint.paint_prefix = "Flexicolour_";
        s.palette.back = "FlexiColourSurround";
        s.palette.swatch_prefix = "Flexicolour_";
        s.palette.ring = "FlexiColourHighlightBlue";
        s.palette.current_ring = "FlexiColourHighlightWhite";
        s.spinner = spinner_style(p);
        s.dropdown = dropdown_style(p);
        s.tick_box = kTickbox;
        s.tick = kTick;
        s.button = kTextButton;
        return s;
    }

    /// The window beside the build menus, in the options window's frame, with the game's theme filters.
    [[nodiscard]] inline BuildPickerStyle build_picker_style(const Palette& p)
    {
        BuildPickerStyle s;
        s.text_colour = p.text;
        s.hint_colour = p.text_disabled;
        s.window = window_style(p);
        s.open_heading = kOpenHeading;
        s.closed_heading = kClosedHeading;
        s.search_box = kListBox;
        s.search_box_focused = kListBoxFocused;
        s.object_filter = kObjectFilter;
        s.object_filter_off = kObjectFilterOff;
        s.object_help = kObjectFilterHelp;
        s.style_filter = kStyleFilter;
        s.style_filter_off = kStyleFilterOff;
        s.pack_filter = kPackBadge;
        s.pack_name = kPackName;
        s.style_help = kStyleFilterHelp;
        s.scroll_bar = scroll_bar_style(p, Orientation::Vertical);
        return s;
    }

    /// From the screen's bottom edge to the bottom-left panels: over the tool row and its open menu.
    inline constexpr float kOverToolRow = tool_row::kMargin + tool_row::kButtonSize + tool_row::kItemGap + tool_row::kItemHeight + 4;

    /// The round panel in the bottom-left corner that lays paths, drawn in code like the info panel. The
    /// controls use the attraction editor's turn and slope pictures, which come in a usable and an unusable
    /// look.
    [[nodiscard]] inline FootpathPanelStyle footpath_panel_style(const Palette& p)
    {
        FootpathPanelStyle s;
        s.text_colour = p.text_on_band;
        s.page_text_colour = p.text;
        s.chosen = p.paper;
        s.look = PanelLook::Coded;
        s.coded = info_panel_style(p).coded;
        s.tab_face = kMenuButtonSmall;
        s.close_icon = kCloseIcon;
        s.bottom = kOverToolRow;
        s.controls_icon = "RideCamGUI_Pin";
        s.follow_icon = "Guest_Camera";
        s.route_icon = "Map";
        s.remove_icon = "PathEditor_IconDeleteOn";
        s.paths_icon = "Obj_Paths_Path";
        s.queues_icon = "Obj_Paths_Queue";
        s.tab_help = { "PathEditor_EditMode", "PathEditor_CameraFollow", "Rct3pp_Paths_RouteHelp", "Rct3pp_Paths_RemoveHelp", "Obj_Paths_PathHelp", "Obj_Paths_QueueHelp" };
        // Each turn and slope picture has a usable and an unusable look; the remove picture has only one.
        const std::array<std::string_view, 8> bases { "AttractionEditor_IconCurveL3", "AttractionEditor_IconCurve0", "AttractionEditor_IconCurveR3", "AttractionEditor_IconSlopeD2", "AttractionEditor_IconSlopeD1", "AttractionEditor_IconSlope0", "AttractionEditor_IconSlopeU1", "AttractionEditor_IconSlopeU2" };
        for (std::size_t i = 0; i < bases.size(); ++i)
        {
            s.usable_icons.at(i) = std::format("{}On", bases[i]);
            s.unusable_icons.at(i) = std::format("{}Dis", bases[i]);
        }
        s.usable_icons.at(8) = "PathEditor_IconDeleteOn";
        s.unusable_icons.at(8) = s.usable_icons.at(8);
        s.cut_on_icon = "PathEditor_CutIntoTerrain_on";
        s.cut_off_icon = "PathEditor_CutIntoTerrain_off";
        s.control_help = {
            "Path_Left", "Path_Straight", "Path_Right", "Path_DownwardSteps", "Path_DownwardSlope", "Path_NoSlope", "Path_UpwardSlope", "Path_UpwardSteps", "PathEditor_Delete", "PathEditor_CutIntoTerrain_TT",
        };
        return s;
    }

    /// The round panel in the bottom-left corner that shapes and paints the ground, drawn in code like the path
    /// panel.
    [[nodiscard]] inline LandPanelStyle land_panel_style(const Palette& p)
    {
        LandPanelStyle s;
        s.text_colour = p.text_on_band;
        s.brush = p.patch;
        s.coded = info_panel_style(p).coded;
        s.tab_face = kMenuButtonSmall;
        s.close_icon = kCloseIcon;
        s.size = spinner_style(p);
        s.bottom = kOverToolRow;
        return s;
    }

    /// The large buttons along the bottom of the park view.
    [[nodiscard]] inline ToolRowStyle tool_row_style(const Palette&)
    {
        ToolRowStyle s;
        s.icons = { "Obj_Delete", "Land", "Obj_Paths", "Obj_Scenery", "Obj_Shops", "Obj_Rides", "Man", "File_Quit", "File", "View", "Map" };
        s.career_leave_icon = "File_CareerMap";
        s.item_face = kMenuButtonSmall;
        return s;
    }

    /// The strip that builds track, its buttons in the game's pictures: each picture's base name, with "On",
    /// "Off" or "Dis" after it for its state.
    [[nodiscard]] inline ConstructionPanelStyle construction_panel_style(const Palette& p)
    {
        ConstructionPanelStyle s;
        s.text_colour = p.text;
        constexpr std::string_view kIcon = "AttractionEditor_Icon";
        const auto named = [&](std::string_view what) { return std::string(kIcon) + std::string(what); };
        s.curves = { named("CurveL3"), named("CurveL2"), named("CurveL1"), named("Curve0"), named("CurveR1"), named("CurveR2"), named("CurveR3") };
        s.slopes = { named("SlopeD3"), named("SlopeD2"), named("SlopeD1"), named("Slope0"), named("SlopeU1"), named("SlopeU2"), named("SlopeU3") };
        s.banks = { named("BankL3"), named("BankL2"), named("BankL1"), named("Bank0"), named("BankR1"), named("BankR2"), named("BankR3"), named("BankInv") };
        s.dial = "AttractionEditor_BankBackground";
        s.chain = named("Chain");
        s.demolish = named("DeleteOn");
        s.complete = named("ForwardOn");
        s.new_segment = "IconAttractionEditorNewTrackSegment";
        s.named_button = kTextButton;
        s.named_inset = kLozengeInset;
        s.page_up = "SquareArrowUp";
        s.page_down = "SquareArrowDown";
        return s;
    }

} // namespace rct3::ui::game_skins
