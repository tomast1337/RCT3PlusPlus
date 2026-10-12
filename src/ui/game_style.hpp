// SPDX-License-Identifier: GPL-3.0-or-later
//
// The skins the game's own interface is drawn with, by the names its data uses, and the measurements that
// go with them. The measurements are ours, chosen to fit the skins.
#pragma once

#include "ui/font_set.hpp"
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

} // namespace rct3::ui::game_skins
