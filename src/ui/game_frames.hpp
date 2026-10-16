// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game's frames drawn in code: for each skin that can stand without its picture, the shapes that draw it
// (coded_frame.hpp). The sizes and colours are ours, measured to read as the game's frames on screen; each
// frame is laid out in the rectangle its skin would be stretched over, its drop shadow included.
#pragma once

#include "ui/coded_frame.hpp"
#include "ui/draw_list.hpp"

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
    /// What the window's content sits on: white with two faint pink lines in every four, running across. One
    /// tile of it, 16 units square; the window repeats it down its body.
    [[nodiscard]] CodedFrame window_background();

    // The front screen's backdrop, in its 1024 by 768 design (front_screen_layout.hpp): round its circle's
    // centre a white veil, blue outside it to the left, and along the bottom the striped blue strip with the
    // white lozenge the help sits on. Each frame is measured on the whole design; `screen` gives the
    // screen's edges in design units, which the blue, the veil and the strip reach out to, so on a screen of
    // another shape the frame is drawn over the design's rectangle at one scale and stays round.

    /// The backdrop under everything else.
    [[nodiscard]] CodedFrame front_backdrop(const Rect& screen);
    /// The white ring over the menu's bands, round the same centre.
    [[nodiscard]] CodedFrame front_arc();
    /// The part of `whole` (a frame measured on the design) that falls in the design rectangle `piece`, as a
    /// frame measured on that piece: for a skin that is one piece of the design's picture.
    [[nodiscard]] CodedFrame front_piece(const CodedFrame& whole, const Rect& piece);

    /// The white half disc behind a menu button, its flat side to the right where the label's band starts.
    [[nodiscard]] CodedFrame label_patch();
    /// The pointed end of a label's band: white below the line from its top-left to its bottom-right.
    [[nodiscard]] CodedFrame label_end();

    /// The loading bar's round ends: light blue with a darker edge.
    [[nodiscard]] CodedFrame bar_end_left();
    [[nodiscard]] CodedFrame bar_end_right();

} // namespace rct3::ui::game_frames
