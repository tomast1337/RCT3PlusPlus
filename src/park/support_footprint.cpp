// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/support_footprint.hpp"

#include "ovl/scenery_item.hpp"

#include <bit>

namespace rct3::park
{

    namespace
    {

        // Item flags.
        constexpr std::uint32_t kCarriesColumns = 0x100;
        constexpr std::uint32_t kWantsSupports = 0x80;
        constexpr std::uint32_t kAlternateTiles = 0x200;
        constexpr std::uint32_t kByPoints = 0x400;
        constexpr std::uint32_t kCapped = 0x800;
        constexpr std::uint32_t kTrackType = 18;

        // Square flags.
        constexpr std::uint32_t kSquareWantsColumn = 0x2;
        constexpr std::uint32_t kFirstCellBit = 3;
        constexpr std::uint32_t kCellMask = 0x1ff;

        // A support point's words.
        constexpr std::size_t kTopSquare = 0;
        constexpr std::size_t kTopCell = 2;
        constexpr std::size_t kTopWithin = 4;
        constexpr std::size_t kTopHeight = 6;
        constexpr std::size_t kColumnSquare = 7;
        constexpr std::size_t kColumnCell = 9;
        constexpr std::size_t kColumnHeight = 11;
        constexpr std::size_t kPointFlags = 20;
        constexpr std::size_t kPointGroup = 21;
        constexpr std::uint32_t kHanging = 0x1;
        constexpr std::uint32_t kStraightOnly = 0x2;

        constexpr SupportKind kLastDiagonal = kFirstDiagonal + 3;

        std::int32_t int_at(const ovl::SceneryItemNode& n, std::size_t i)
        {
            return static_cast<std::int32_t>(n.words[i]);
        }

        float float_at(const ovl::SceneryItemNode& n, std::size_t i)
        {
            return std::bit_cast<float>(n.words[i]);
        }

    } // namespace

    SupportKind turned(SupportKind kind, std::int32_t quarter_turns)
    {
        const std::int32_t turns = ((quarter_turns % 4) + 4) % 4;
        if (kind >= kFirstDiagonal && kind <= kLastDiagonal)
            return kFirstDiagonal + (kind - kFirstDiagonal + static_cast<SupportKind>(turns)) % 4;
        if ((kind == kAlongX || kind == kAlongZ) && turns % 2 == 1)
            return kind == kAlongX ? kAlongZ : kAlongX;
        return kind;
    }

    std::optional<SupportFootprint> support_footprint(const ovl::SceneryItem& item)
    {
        SupportFootprint f;
        f.wants_supports = (item.flags & kWantsSupports) != 0;
        f.carries_columns = (item.flags & kCarriesColumns) != 0;
        f.track = item.type == kTrackType;
        if ((!f.wants_supports && !f.carries_columns && !f.track) || item.squares.empty())
            return std::nullopt;
        f.by_points = (item.flags & kByPoints) != 0;
        f.alternate_tiles = (item.flags & kAlternateTiles) != 0;
        f.capped = (item.flags & kCapped) != 0;
        f.x_squares = static_cast<std::int32_t>(item.x_squares);
        f.z_squares = static_cast<std::int32_t>(item.z_squares);
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

} // namespace rct3::park
