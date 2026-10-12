// SPDX-License-Identifier: GPL-3.0-or-later
//
// GUI skin items: which part of a texture draws a piece of the interface, as one rectangle or as a
// stretchable grid of rectangles.
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "ovl/overlay.hpp"

namespace rct3::ovl {

/// How a skin item cuts up its texture. Values of `GuiSkin::kind` seen in files.
enum class GuiSkinKind : std::uint32_t {
  Rect = 0,             ///< One rectangle: x0, y0, x1, y1
  Solid = 1,            ///< No texture area: a single item in the install, with no coordinates
  NinePatch = 2,        ///< Four x lines then four y lines: a 3 x 3 grid
  Frame = 3,            ///< Same lines as NinePatch, the centre cell is not drawn
  HorizontalThree = 4,  ///< Four x lines then two y lines: three cells side by side
  VerticalThree = 5,    ///< Two x lines then four y lines: three cells stacked
};

struct GuiSkin {
  std::uint32_t kind = 0;  ///< A `GuiSkinKind` value
  std::string_view texture;  ///< "tex" item the coordinates refer to. Empty when not referenced.
  /// Texture coordinates in pixels, in the order the kind lists them: 4, 0, 8, 8, 6 or 6 values.
  std::vector<std::int32_t> coordinates;
};

struct GuiSkinItem {
  std::string_view name;           ///< Symbol name without the tag
  std::optional<GuiSkin> skin;     ///< Nothing when the kind is unknown or the coordinates are unreadable
};

/// GUI skin items of one part of an overlay: pointer symbols tagged "gsi". Views in the result point
/// into the overlay's bytes.
[[nodiscard]] std::vector<GuiSkinItem> read_gui_skins(const Overlay& overlay, const Part& part);

}  // namespace rct3::ovl
