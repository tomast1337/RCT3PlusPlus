// SPDX-License-Identifier: GPL-3.0-or-later
//
// Track sections: the pieces a tracked ride's track is built from (straights, curves, slopes, loops and
// so on). Each ties a piece to its scenery item and to the splines the cars follow along it.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "ovl/item_record.hpp"
#include "ovl/overlay.hpp"

namespace rct3::ovl {

struct TrackSection {
  std::string_view name;  ///< Name string stored in the item, the same as the symbol name in all files
  /// "sid" item. Has the same name as the section in most items, and is empty or another name in the
  /// rest.
  std::string_view scenery_item;
  /// Two small numbers that go together: both 0, both 7, or a value of 1 to 6 with the other being the
  /// value three steps on (1 and 4, 2 and 5, 3 and 6, then back). Meaning unconfirmed.
  std::uint32_t join_a = 0;
  std::uint32_t join_b = 0;
  /// Bit sets. Version 1 files use small numbers, later files have 0x400000 set in nearly all items.
  /// Bit meanings unconfirmed.
  std::array<std::uint32_t, 2> flags{};
  /// "spl" items in file order. Named like `<Piece>L_spline` and `<Piece>R_spline`, so left, right, left,
  /// right. The second pair repeats the first in all but a few items.
  std::array<std::string_view, 4> splines;
  /// Further "spl" items, empty in most pieces.
  std::array<std::string_view, 2> extra_splines;
  std::array<float, 3> scale{};  ///< 1, 1, 1 in nearly all items
  /// Every reference in the record, in file order, including those above.
  std::vector<ItemReference> references;
  /// The whole record: 228 bytes in version 1 files, 364 in version 4 and 392 in version 5. Fields not
  /// named above are unconfirmed.
  std::span<const std::byte> record;
};

struct TrackSectionItem {
  std::string_view name;                 ///< Symbol name without the tag
  std::optional<TrackSection> section;   ///< Nothing when the record does not fit its block
};

/// Track sections of one part of an overlay: pointer symbols tagged "tks". Views in the result point
/// into the overlay's bytes.
[[nodiscard]] std::vector<TrackSectionItem> read_track_sections(const Overlay& overlay, const Part& part);

}  // namespace rct3::ovl
