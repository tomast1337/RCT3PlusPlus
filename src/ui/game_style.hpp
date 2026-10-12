// SPDX-License-Identifier: GPL-3.0-or-later
//
// The skins the game's own interface is drawn with, by the names its data uses, and the measurements that
// go with them. The measurements are ours, chosen to fit the skins.
#pragma once

#include "ui/font_set.hpp"
#include "ui/tooltip.hpp"
#include "ui/widgets/window.hpp"

#include <array>
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
    inline constexpr std::string_view kTooltip = "GUI2Swatch_Background";
    /// The game passes text buttons their skin case by case; this lozenge is our default for them.
    inline constexpr std::string_view kTextButton = "GUI2Swatch_Background";
    /// What the lozenge's border and drop shadow take from each side (ours, measured on the skin).
    inline constexpr Rect kLozengeInset { 3, 2, 5, 5 };

    /// The fixed-size interface fonts, by role.
    inline constexpr std::array<std::pair<FontRole, std::string_view>, kFontRoleCount> kFonts = { {
        { FontRole::Tiny, "GUIFontTiny" },
        { FontRole::Small, "GUIFontSmall" },
        { FontRole::Medium, "GUIFontMedium" },
        { FontRole::Large, "GUIFontLarge" },
    } };

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
