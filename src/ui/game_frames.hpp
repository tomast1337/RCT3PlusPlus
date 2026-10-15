// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game's frames drawn in code: for each skin that can stand without its picture, the shapes that draw it
// (coded_frame.hpp). The sizes and colours are ours, measured to read as the game's frames on screen; each
// frame is laid out in the rectangle its skin would be stretched over, its drop shadow included.
#pragma once

#include "ui/coded_frame.hpp"

#include <cstdint>

namespace rct3::ui::game_frames
{

    /// The white lozenge with a blue border and a drop shadow: text buttons, tabs, tooltips, menus.
    [[nodiscard]] CodedFrame lozenge();

    /// The rounded light blue patch under a selected row or a highlighted menu item.
    [[nodiscard]] CodedFrame selected_row();

    /// The small round blue button face, darker at the top, behind the tool row's and tabs' icons.
    [[nodiscard]] CodedFrame button_face();

    /// The front screen's big light blue disc behind a menu icon.
    [[nodiscard]] CodedFrame menu_circle();

    enum class Quarter : std::uint8_t
    {
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight,
    };

    /// A quarter of the front screen's big white disc with its blue line, `quarter` naming where it sits.
    [[nodiscard]] CodedFrame quarter_circle(Quarter quarter);

    /// The message drawer: its back, repeated up the drawer, and the two halves of the lip along its bottom.
    [[nodiscard]] CodedFrame drawer_back();
    [[nodiscard]] CodedFrame drawer_lip_left();
    [[nodiscard]] CodedFrame drawer_lip_right();

    /// The small window's frame: a blue band along the top, thin grey sides, a drop shadow.
    [[nodiscard]] CodedFrame mini_window();
    /// The white panel the small window's content sits on.
    [[nodiscard]] CodedFrame mini_window_panel();

    /// The window frame: the blue band with a ring cut round the icon's disc, thin grey sides, a drop shadow.
    [[nodiscard]] CodedFrame window();

} // namespace rct3::ui::game_frames
