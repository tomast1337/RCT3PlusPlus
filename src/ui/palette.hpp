// SPDX-License-Identifier: GPL-3.0-or-later
//
// The interface's colours by role. The frames drawn in code (game_frames.hpp) and the game's styles
// (game_style.hpp) take theirs from a palette, so the whole look can be swapped, for a dark one say, by
// building them from another. A palette made with no changes is the game's own look, as we measure it on
// screen.
//
// Shadows stay as they are in every palette, and so do colours that carry meaning rather than look: graph
// series, the map, money gained and spent.
//
// shortcut: colours only the widget headers set (rating bars, hover tints, the slider's rail, the options
// bar) keep their fixed values; give each a role when a palette needs it changed.
#pragma once

#include "ui/draw_list.hpp"

namespace rct3::ui
{

    struct Palette
    {
        // Ink.
        Color text = hex("#000000");
        Color text_disabled = hex("#7f7f7f");
        Color text_on_band = hex("#ffffff"); ///< On the blue strips, panels and bars
        Color accent = hex("#2484be");       ///< Window bands, captions, headings and hovered names
        Color selection = hex("#5aa8e6");    ///< Behind a highlighted row or menu item
        Color rule = hex("#000000");         ///< Title rules, the drawer's grip, the slider arrows' edges

        // Paper: what content sits on, and the lines on it.
        Color paper = hex("#ffffff");
        Color paper_tint = hex("#fffbff"); ///< Where the game's pictures turn faintly pink
        Color paper_line = hex("#fff7ff"); ///< The fine lines across a window's body, in pairs
        Color paper_line_deep = hex("#fff4ff");
        Color stripe = hex("#efe8ef");     ///< Behind every other row, or the chosen one
        Color side = hex("#c8c5c2");       ///< The thin sides of windows and the drawer
        Color panel_edge = hex("#cdcdcd"); ///< Round the small window's panel
        Color box_line = hex("#bea9a9");   ///< Round boxes, lists and page arrows
        Color button_rim = hex("#c8c1c1");
        Color tick_line = hex("#6b696b");

        // The blues of frames and controls.
        Color border = hex("#085fa2"); ///< Round lozenges; the face of scroll arrows and riders
        Color patch = hex("#56abe5");  ///< Selected rows, the park bar, discs
        Color patch_border = hex("#4099d6");
        Color disc_border = hex("#2a79af");
        Color icon = hex("#44c7ff"); ///< The window's icon disc, the light on button faces
        Color face_top = hex("#3a94c4");
        Color face_rim = hex("#2072b0");
        Color line = hex("#52aae7"); ///< Round the front screen's disc, across its strip
        Color spinner = hex("#1f70ae");
        Color strip = hex("#4babe4"); ///< The round panels' strips, the title button
        Color backdrop = hex("#2979ad");

        // Controls that cannot be used.
        Color disabled_light = hex("#ededed");
        Color disabled_fill = hex("#c1c1c1");
        Color disabled_rim = hex("#dedede");
        Color disabled_spinner = hex("#c7c7c7");

        // Small parts.
        Color heading_box = hex("#d5d8d4");
        Color heading_sign = hex("#535b5d");
        Color slider_arrow = hex("#36ca4a");
        Color colour_ring = hex("#504f8c"); ///< Behind a colour in the colour picker
        Color colour_current = hex("#ffffff");
        Color colour_back = hex("#74c0ea");
        Color colour_back_line = hex("#1f73b0");

        // The front screen.
        Color veil = hex("#fbf9f8c0"); ///< Round the menu's disc
        Color under_ring = hex("#ffffffee");
        Color ring = hex("#f7f3f7");
        Color title_paper = hex("#fbf8f5");
        Color label_band = hex("#f7f2f2");
    };

    /// The game's own look.
    inline constexpr Palette kGamePalette { };

} // namespace rct3::ui
