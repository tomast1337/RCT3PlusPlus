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

    /// The white lozenge with a blue border and a drop shadow: text buttons, tabs, tooltips, menus. A button
    /// that cannot be used keeps it white with a pale grey border, so its grey text still reads.
    [[nodiscard]] CodedFrame lozenge(bool enabled = true);

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

    /// The light blue strip a ring panel's tools sit on, with a thin dark blue edge, square on the left where
    /// the ring covers it and round on the right, over a drop shadow.
    [[nodiscard]] CodedFrame curved_strip();

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

    /// Which way an arrow on a button points.
    enum class Arrow : std::uint8_t
    {
        Up,
        Down,
        Left,
        Right,
    };

    // The small controls, each measured at the size of the picture it stands in for.

    /// The frame round lists, text boxes and other boxed content: a thin pinkish grey line, clear inside.
    [[nodiscard]] CodedFrame plain_box();
    /// A text box with the keyboard: a blue line two units wide, clear inside.
    [[nodiscard]] CodedFrame focused_box();
    /// The round white button with a faint grey rim, 30 units across.
    [[nodiscard]] CodedFrame round_button();
    /// What a scroll bar's rider runs along: white.
    [[nodiscard]] CodedFrame scroll_rail();
    /// A scroll bar's rider: dark blue inside a white line.
    [[nodiscard]] CodedFrame scroll_rider();
    /// The arrow at a scroll bar's end, 15 units square: a white arrow on dark blue inside a white line.
    [[nodiscard]] CodedFrame scroll_arrow(Arrow way);
    /// A list's opener that cannot be used: as a downward scroll arrow, in pale greys.
    [[nodiscard]] CodedFrame opener_disabled();
    /// A page arrow, 17 units square: a scroll arrow inside a further pinkish grey line; pale greys when it
    /// cannot be used.
    [[nodiscard]] CodedFrame square_arrow(Arrow way, bool enabled);
    /// One half of a spinner, 20 units by 8: a small white arrow on blue inside a white line; pale greys when
    /// it cannot be used.
    [[nodiscard]] CodedFrame spinner(Arrow way, bool enabled);
    /// A tick box, 12 units square: white, with a dark grey line one unit in.
    [[nodiscard]] CodedFrame tickbox();
    /// The light blue disc a window's close button sits on, 14 units across.
    [[nodiscard]] CodedFrame title_button();
    /// A list heading's box, 7 units square: pale grey with a dark minus, and a plus when `closed`.
    [[nodiscard]] CodedFrame heading_box(bool closed);
    /// A slider's thumb, 18 units by 13: two green arrows, left and right, outlined in black.
    [[nodiscard]] CodedFrame slider_thumb();
    /// The disc behind a colour in the colour picker, `size` units across.
    [[nodiscard]] CodedFrame colour_ring(Color colour, float size);
    /// What the colour picker sits on, 34 units by 37: light blue in a dark blue line, round, over a drop shadow.
    [[nodiscard]] CodedFrame colour_back();

    /// The park bar along the screen's top: a light blue band over a dark shadow, its outer bottom corners
    /// round. The middle piece, `width` by `height` units, has a half disc notch of radius `notch` up from its
    /// bottom edge.
    [[nodiscard]] CodedFrame park_bar_left();
    [[nodiscard]] CodedFrame park_bar_middle(float width, float height, float notch);
    [[nodiscard]] CodedFrame park_bar_right();

    /// The white half disc behind a menu button, its flat side to the right where the label's band starts.
    [[nodiscard]] CodedFrame label_patch();
    /// The pointed end of a label's band: white below the line from its top-left to its bottom-right.
    [[nodiscard]] CodedFrame label_end();

} // namespace rct3::ui::game_frames
