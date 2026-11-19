// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scenery items: the description of something that can be placed in a park (scenery, walls, supports,
// track pieces, stalls, rides ...). The structure exists in three sizes, selected by the item itself:
// the base one and two that add data for the first and second expansion. A fourth, shorter form with
// no bounding box occurs in a single file of the base game.
#pragma once

#include "core/types.hpp"
#include "ovl/overlay.hpp"
#include "ovl/pointer_fields.hpp"
#include "ovl/reference_names.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    /// One tile of an item's footprint.
    struct SceneryItemSquare
    {
        /// Bit mask. Bit 0 is believed to enable collision and bit 1 to show supports. The remaining bits
        /// (up to 0xFFF seen) are unconfirmed.
        u32 flags = 0;
        i32 min_height = 0; ///< Lowest occupied level. -1 and -2 occur.
        i32 max_height = 0; ///< Highest occupied level, 0 for tiles without collision.
        /// Bit masks, one 32-bit word per entry, usually one. Bit n set means level n is occupied. Empty for
        /// tiles without collision, whose array pointer is null.
        std::vector<u32> height_masks;
        /// The square's last word, read above as the number of mask words. In track pieces and support pieces
        /// it tells which way a support column under the square faces: 0 none, 1 any way, 2 to 5 one of four
        /// diagonal turns, 6 and 7 along one axis or the other. In the track and support files checked the
        /// values pair up that way between track squares and support pieces, and reading them as a count runs
        /// into the next square's words, so the count reading is likely wrong for those items.
        u32 support_kind = 0;
    };

    /// A 22-word record from the optional per-item array that follows the footprint. Every record is 88
    /// bytes. The fields at words 4..6 and 11 are floats that look like a position, words 12 to 16 are
    /// zero and words 17 and 18 are 0.5 in every file seen. Nothing else is confirmed.
    struct SceneryItemNode
    {
        std::array<u32, 22> words { };
    };

    /// A name and a value, both strings in the file.
    struct SceneryItemParameter
    {
        std::string_view name;
        std::string_view value;
    };

    /// What one step of a sound script does.
    enum class SoundCue : u32
    {
        Start = 1,  ///< Start one of the entry's sounds, chosen at random
        Stop = 2,   ///< Stop the sound started last
        Volume = 3, ///< Bring the volume to `value` by `until`, in a straight line
        Pitch = 4,  ///< Bring the pitch to `value` by `until`, in a straight line
    };

    /// One step of a sound script: at `at` seconds into its animation, do `cue`.
    struct SoundStep
    {
        f32 at = 0;
        SoundCue cue = SoundCue::Start;
        f32 value = 0; ///< Volume and Pitch only
        f32 until = 0; ///< Volume and Pitch only: seconds into the animation the change is done by; at once when not later than `at`
        friend bool operator==(const SoundStep&, const SoundStep&) = default;
    };

    /// One entry of the sound table.
    struct SceneryItemSound
    {
        std::vector<std::string_view> sounds; ///< "snd" items, one is chosen when the entry plays
        /// One script per animation of the item, in the item's animation order; empty where that animation is silent.
        std::vector<std::vector<SoundStep>> scripts;
    };

    /// A row of 12 bytes naming a research category and the level it must reach. Scenery items (three items of the
    /// oldest file only), path types, queue types and research categories (their prerequisites) hold them.
    struct SceneryItemResearch
    {
        std::string_view category; ///< "rcg" item
        i32 unknown = 0;           ///< -1 in files seen
        f32 level = 0;             ///< Level the category must reach, 0 to 100 in files seen
    };

    /// SceneryItem::flags bit of path items that stand only on level paths: benches and most signs.
    inline constexpr u32 kItemLevelPathsOnly = 1u << 0;
    /// SceneryItem::flags bits of path items that stand only off queues, or only on queues. No item the game ships
    /// sets either; custom items may.
    inline constexpr u32 kItemQueuesOnly = 1u << 17;
    inline constexpr u32 kItemOffQueues = 1u << 18;
    /// SceneryItem::flags bit set on flat rides, stalls, shops and a few animated statues: the ground under them
    /// is not reshaped.
    inline constexpr u32 kItemHoldsGround = 1u << 3;
    /// SceneryItem::flags bit of items whose shape bends to fit the ground under it (plants, rocks, benches).
    inline constexpr u32 kItemFitsGround = 1u << 12;
    /// SceneryItem::flags bit of items that stand at the ground's height where they are; without it an item
    /// keeps the whole-unit height the park stored, as walls do.
    inline constexpr u32 kItemOnGroundHeight = 1u << 13;
    /// SceneryItem::flags bit of rides that stand only on water. They get no fence round them.
    inline constexpr u32 kItemWaterOnly = 1u << 16;
    /// SceneryItem::flags bit of rides fenced round the outline of their solid ground squares rather than round
    /// their whole rectangle, for footprints that are not filled in.
    inline constexpr u32 kItemFenceOutline = 1u << 21;
    /// SceneryItemSquare::flags bit of solid squares.
    inline constexpr u32 kSquareSolid = 0x1;

    struct SceneryItem
    {
        /// 0 for the base structure, 1 and 2 for the structures of the first and second expansion.
        u32 version = 0;
        /// True for the shorter base form, which has no bounding box, no node array and no group name.
        /// Those fields are left empty then.
        bool compact = false;

        u32 flags = 0;          ///< Bit mask: 0x10, 0x20, 0x100, 0x1000, 0x2000 ... Partly unconfirmed.
        u16 positioning = 0;    ///< How the item snaps to the grid, 0 to 8 in files seen
        u32 height_squares = 0; ///< Item height in tiles when set, otherwise 0
        u32 x_squares = 0;
        u32 z_squares = 0;
        std::vector<SceneryItemSquare> squares; ///< x_squares * z_squares entries, row by row

        /// Bounding box corners in metres. A box that was never filled holds +-1e9.
        std::array<f32, 3> bounds_min { };
        std::array<f32, 3> bounds_max { };
        std::vector<SceneryItemNode> nodes;

        i32 cost = 0;         ///< Price, never negative in files seen
        i32 removal_cost = 0; ///< Signed: zero, positive and negative values all occur
        u32 unknown_44 = 0;   ///< 0, 4 or 5. Unconfirmed.
        u32 type = 0;         ///< Item category, 0 to 46 seen (some values unused); see park/item_category.hpp

        std::string_view group;                ///< Support set or group name, may be empty
        std::vector<std::string_view> visuals; ///< "svd" items, one to four
        std::string_view icon;                 ///< "gsi" item
        std::string_view group_icon;           ///< "gsi" item
        std::string_view group_text;           ///< "txt" item
        std::string_view source_path;          ///< Path of the resource the item was built from
        std::vector<SceneryItemParameter> parameters;
        std::vector<SceneryItemSound> sounds;
        std::string_view name_text; ///< "txt" item with the display name
        std::vector<SceneryItemResearch> research;

        /// Two words at the same place in every layout whose meanings are not known. 1440 and 2880 seen.
        std::array<u32, 2> unknown_84 { };
        std::array<u32, 3> default_colours { }; ///< Palette indices, 0 when unset
        /// Words that are zero in all but a few items (a count and an address in one pair of items).
        std::array<u32, 15> late_words { };

        /// Versions 1 and 2 only: which expansion the item belongs to (0 base game, 1 first, 2 second).
        u32 addon = 0;
        u32 addon_extra = 0;   ///< Versions 1 and 2. 0, 1 or 2.
        f32 unknown_float = 0; ///< Version 2 only, -1 in nearly all items
        i32 unknown_int = 0;   ///< Version 2 only, -1 in nearly all items
        /// Versions 1 and 2: the item points at a further structure with flat-ride animation data. It is
        /// not decoded.
        bool has_ride_data = false;
    };

    struct SceneryItemEntry
    {
        std::string_view name;           ///< Symbol name without the tag
        std::optional<SceneryItem> item; ///< Nothing when any part of the structure is unreadable
    };

    /// Reads item structures from one part of an overlay. Other item types embed the same structure, so
    /// this is shared with them.
    class SceneryItemReader
    {
    public:
        /// Views in the results point into the overlay's bytes, so the overlay must outlive them.
        SceneryItemReader(const Overlay& overlay, const Part& part);

        /// The structure at `address`. `allow_compact` accepts the shorter base form, which is only told
        /// apart by its block ending early, so it is off for structures embedded among other data.
        [[nodiscard]] std::optional<SceneryItem> read(u32 address, bool allow_compact = true) const;

        /// Structures stored back to back from `address` until the end of the block that holds it. Each is
        /// followed by the visual reference slots it needs beyond the first. Nothing when any structure is
        /// unreadable or the data does not end exactly with the block.
        [[nodiscard]] std::optional<std::vector<SceneryItem>> read_run(u32 address) const;

        /// Research link rows (12 bytes each) at `address`, as stored by scenery items and the types that
        /// embed them. False when the rows do not fit.
        bool read_research(u32 address, u32 count, std::vector<SceneryItemResearch>& out) const;

        /// Address just past the block of the reader's part that holds `address`.
        [[nodiscard]] std::optional<u32> block_end(u32 address) const noexcept;

        /// The AddressSpace the reader resolves pointers in.
        [[nodiscard]] const AddressSpace& space() const noexcept
        {
            return space_;
        }
        [[nodiscard]] const ReferenceNames& references() const noexcept
        {
            return refs_;
        }
        [[nodiscard]] const PointerFields& pointers() const noexcept
        {
            return pointers_;
        }

    private:
        [[nodiscard]] static usize stride_of(const SceneryItem& item) noexcept;
        const Part* part_;
        AddressSpace space_;
        ReferenceNames refs_;
        PointerFields pointers_;
    };

    /// Scenery items of one part of an overlay: pointer symbols tagged "sid". Views in the result point into
    /// the overlay's bytes.
    [[nodiscard]] std::vector<SceneryItemEntry> read_scenery_items(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
