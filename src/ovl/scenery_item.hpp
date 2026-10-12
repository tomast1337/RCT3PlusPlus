// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scenery items: the description of something that can be placed in a park (scenery, walls, supports,
// track pieces, stalls, rides ...). The structure exists in three sizes, selected by the item itself:
// the base one and two that add data for the first and second expansion. A fourth, shorter form with
// no bounding box occurs in a single file of the base game.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "ovl/overlay.hpp"
#include "ovl/pointer_fields.hpp"
#include "ovl/reference_names.hpp"

namespace rct3::ovl {

/// One tile of an item's footprint.
struct SceneryItemSquare {
  /// Bit mask. Bit 0 is believed to enable collision and bit 1 to show supports. The remaining bits
  /// (up to 0xFFF seen) are unconfirmed.
  std::uint32_t flags = 0;
  std::int32_t min_height = 0;  ///< Lowest occupied level. -1 and -2 occur.
  std::int32_t max_height = 0;  ///< Highest occupied level, 0 for tiles without collision.
  /// Bit masks, one 32-bit word per entry, usually one. Bit n set means level n is occupied. Empty for
  /// tiles without collision, whose array pointer is null.
  std::vector<std::uint32_t> height_masks;
};

/// A 22-word record from the optional per-item array that follows the footprint. Every record is 88
/// bytes. The fields at words 4..6 and 11 are floats that look like a position, words 12 to 16 are
/// zero and words 17 and 18 are 0.5 in every file seen. Nothing else is confirmed.
struct SceneryItemNode {
  std::array<std::uint32_t, 22> words{};
};

/// A name and a value, both strings in the file.
struct SceneryItemParameter {
  std::string_view name;
  std::string_view value;
};

/// One entry of the sound table.
struct SceneryItemSound {
  std::vector<std::string_view> sounds;  ///< "snd" items, one is chosen when the entry plays
  /// Number of script slots that follow. The scripts themselves are not decoded.
  std::uint32_t script_count = 0;
};

/// Link to a research category, present on three items of the oldest file only.
struct SceneryItemResearch {
  std::string_view category;  ///< "rcg" item
  std::int32_t unknown = 0;   ///< -1 in files seen
  float priority = 0;         ///< Unconfirmed. 10 in files seen.
};

struct SceneryItem {
  /// 0 for the base structure, 1 and 2 for the structures of the first and second expansion.
  std::uint32_t version = 0;
  /// True for the shorter base form, which has no bounding box, no node array and no group name.
  /// Those fields are left empty then.
  bool compact = false;

  std::uint32_t flags = 0;        ///< Bit mask: 0x10, 0x20, 0x100, 0x1000, 0x2000 ... Partly unconfirmed.
  std::uint16_t positioning = 0;  ///< How the item snaps to the grid, 0 to 8 in files seen
  std::uint32_t height_squares = 0;  ///< Item height in tiles when set, otherwise 0
  std::uint32_t x_squares = 0;
  std::uint32_t z_squares = 0;
  std::vector<SceneryItemSquare> squares;  ///< x_squares * z_squares entries, row by row

  /// Bounding box corners in metres. A box that was never filled holds +-1e9.
  std::array<float, 3> bounds_min{};
  std::array<float, 3> bounds_max{};
  std::vector<SceneryItemNode> nodes;

  std::int32_t cost = 0;          ///< Price, never negative in files seen
  std::int32_t removal_cost = 0;  ///< Signed: zero, positive and negative values all occur
  std::uint32_t unknown_44 = 0;   ///< 0, 4 or 5. Unconfirmed.
  std::uint32_t type = 0;         ///< Item category, 0 to 46 seen (some values unused). The meaning of each value is not confirmed here.

  std::string_view group;          ///< Support set or group name, may be empty
  std::vector<std::string_view> visuals;  ///< "svd" items, one to four
  std::string_view icon;           ///< "gsi" item
  std::string_view group_icon;     ///< "gsi" item
  std::string_view group_text;     ///< "txt" item
  std::string_view source_path;    ///< Path of the resource the item was built from
  std::vector<SceneryItemParameter> parameters;
  std::vector<SceneryItemSound> sounds;
  std::string_view name_text;      ///< "txt" item with the display name
  std::vector<SceneryItemResearch> research;

  /// Two words at the same place in every layout whose meanings are not known. 1440 and 2880 seen.
  std::array<std::uint32_t, 2> unknown_84{};
  std::array<std::uint32_t, 3> default_colours{};  ///< Palette indices, 0 when unset
  /// Words that are zero in all but a few items (a count and an address in one pair of items).
  std::array<std::uint32_t, 15> late_words{};

  /// Versions 1 and 2 only: which expansion the item belongs to (0 base game, 1 first, 2 second).
  std::uint32_t addon = 0;
  std::uint32_t addon_extra = 0;  ///< Versions 1 and 2. 0, 1 or 2.
  float unknown_float = 0;        ///< Version 2 only, -1 in nearly all items
  std::int32_t unknown_int = 0;   ///< Version 2 only, -1 in nearly all items
  /// Versions 1 and 2: the item points at a further structure with flat-ride animation data. It is
  /// not decoded.
  bool has_ride_data = false;
};

struct SceneryItemEntry {
  std::string_view name;               ///< Symbol name without the tag
  std::optional<SceneryItem> item;     ///< Nothing when any part of the structure is unreadable
};

/// Reads item structures from one part of an overlay. Other item types embed the same structure, so
/// this is shared with them.
class SceneryItemReader {
 public:
  /// Views in the results point into the overlay's bytes, so the overlay must outlive them.
  SceneryItemReader(const Overlay& overlay, const Part& part);

  /// The structure at `address`. `allow_compact` accepts the shorter base form, which is only told
  /// apart by its block ending early, so it is off for structures embedded among other data.
  [[nodiscard]] std::optional<SceneryItem> read(std::uint32_t address, bool allow_compact = true) const;

  /// Structures stored back to back from `address` until the end of the block that holds it. Each is
  /// followed by the visual reference slots it needs beyond the first. Nothing when any structure is
  /// unreadable or the data does not end exactly with the block.
  [[nodiscard]] std::optional<std::vector<SceneryItem>> read_run(std::uint32_t address) const;

  /// Research link rows (12 bytes each) at `address`, as stored by scenery items and the types that
  /// embed them. False when the rows do not fit.
  bool read_research(std::uint32_t address, std::uint32_t count, std::vector<SceneryItemResearch>& out) const;

  /// Address just past the block of the reader's part that holds `address`.
  [[nodiscard]] std::optional<std::uint32_t> block_end(std::uint32_t address) const noexcept;

  /// The AddressSpace the reader resolves pointers in.
  [[nodiscard]] const AddressSpace& space() const noexcept { return space_; }
  [[nodiscard]] const ReferenceNames& references() const noexcept { return refs_; }
  [[nodiscard]] const PointerFields& pointers() const noexcept { return pointers_; }

 private:
  [[nodiscard]] static std::size_t stride_of(const SceneryItem& item) noexcept;
  const Part* part_;
  AddressSpace space_;
  ReferenceNames refs_;
  PointerFields pointers_;
};

/// Scenery items of one part of an overlay: pointer symbols tagged "sid". Views in the result point into
/// the overlay's bytes.
[[nodiscard]] std::vector<SceneryItemEntry> read_scenery_items(const Overlay& overlay, const Part& part);

}  // namespace rct3::ovl
