// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/support/footprint.hpp"

#include "core/types.hpp"
#include "ovl/scenery_item.hpp"

#include <bit>

namespace rct3::park::support
{

    namespace
    {

        // Item flags.
        constexpr u32 kCarriesColumns = 0x100;
        constexpr u32 kWantsSupports = 0x80;
        constexpr u32 kAlternateTiles = 0x200;
        constexpr u32 kByPoints = 0x400;
        constexpr u32 kCapped = 0x800;
        constexpr u32 kNoColumnsMeet = 1u << 26; ///< Joined coaster pieces: columns neither hold them up nor stop at them
        constexpr u32 kTrackType = 18;

        // Square flags.
        constexpr u32 kSquareWantsColumn = 0x2;
        constexpr u32 kFirstCellBit = 3;
        constexpr u32 kCellMask = 0x1ff;

        // A support point's words.
        constexpr usize kTopSquare = 0;
        constexpr usize kTopCell = 2;
        constexpr usize kTopWithin = 4;
        constexpr usize kTopHeight = 6;
        constexpr usize kColumnSquare = 7;
        constexpr usize kColumnCell = 9;
        constexpr usize kColumnHeight = 11;
        constexpr usize kPointFlags = 20;
        constexpr usize kPointGroup = 21;
        constexpr u32 kHanging = 0x1;
        constexpr u32 kStraightOnly = 0x2;

        constexpr SupportKind kLastDiagonal = kFirstDiagonal + 3;

        i32 int_at(const ovl::SceneryItemNode& n, usize i)
        {
            return static_cast<i32>(n.words[i]);
        }

        f32 float_at(const ovl::SceneryItemNode& n, usize i)
        {
            return std::bit_cast<f32>(n.words[i]);
        }

    } // namespace

    SupportKind turned(SupportKind kind, ModelTurn turn)
    {
        if (kind >= kFirstDiagonal && kind <= kLastDiagonal)
            return kFirstDiagonal + (kind - kFirstDiagonal + static_cast<SupportKind>(turn.value)) % 4;
        if ((kind == kAlongX || kind == kAlongZ) && turn.value % 2 == 1)
            return kind == kAlongX ? kAlongZ : kAlongX;
        return kind;
    }

    std::optional<SupportFootprint> support_footprint(const ovl::SceneryItem& item)
    {
        SupportFootprint f;
        f.wants_supports = (item.flags & kWantsSupports) != 0;
        f.carries_columns = (item.flags & kCarriesColumns) != 0;
        f.track = item.type == kTrackType;
        if ((item.flags & kNoColumnsMeet) != 0 || item.squares.empty())
            return std::nullopt;
        f.by_points = (item.flags & kByPoints) != 0;
        f.alternate_tiles = (item.flags & kAlternateTiles) != 0;
        f.capped = (item.flags & kCapped) != 0;
        f.x_squares = static_cast<i32>(item.x_squares);
        f.z_squares = static_cast<i32>(item.z_squares);
        for (const ovl::SceneryItemSquare& s : item.squares)
            f.squares.push_back({ s.min_height, s.max_height, s.support_kind, (s.flags & kSquareWantsColumn) != 0, (s.flags >> kFirstCellBit) & kCellMask });
        if (f.by_points)
        {
            for (const ovl::SceneryItemNode& n : item.nodes)
            {
                SupportPoint p;
                p.top = { { int_at(n, kTopSquare), int_at(n, kTopSquare + 1) }, { int_at(n, kTopCell), int_at(n, kTopCell + 1) }, { float_at(n, kTopWithin), float_at(n, kTopWithin + 1) } };
                p.top_height = float_at(n, kTopHeight);
                p.column = { { int_at(n, kColumnSquare), int_at(n, kColumnSquare + 1) }, { int_at(n, kColumnCell), int_at(n, kColumnCell + 1) } };
                p.column_height = float_at(n, kColumnHeight);
                p.hanging = (n.words[kPointFlags] & kHanging) != 0;
                p.straight_only = (n.words[kPointFlags] & kStraightOnly) != 0;
                p.group = n.words[kPointGroup];
                f.points.push_back(p);
            }
        }
        return f;
    }

} // namespace rct3::park::support
