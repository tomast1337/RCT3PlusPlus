// SPDX-License-Identifier: GPL-3.0-or-later
//
// The pieces support columns are built from. Every support piece is a scenery item that names its set by
// its group, and says by its item type what it is: a middle piece of some height to stack, a foot that
// stands on something, a deeper foot that reaches into the ground below it for ground that is not level, a
// top piece that meets the thing held up, a cap for the top of a column, or a beam. Pieces also say which
// way they face (see support/footprint.hpp); a group may hold one set of pieces for columns that face any
// way and others for columns that face along an axis or a diagonal.
#pragma once

#include "core/types.hpp"
#include "park/support/footprint.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::park::support
{

    /// Item types of support pieces, as scenery items store them.
    inline constexpr u32 kSupportMiddleType = 12;
    inline constexpr u32 kSupportTopType = 13;
    inline constexpr u32 kSupportFootType = 14;
    inline constexpr u32 kSupportDeepFootType = 15;
    inline constexpr u32 kSupportBeamType = 16;
    inline constexpr u32 kSupportCapType = 17;

    /// Where the support pieces are, relative to the game's folder and as PlacedObject::overlay names it.
    inline constexpr std::string_view kSupportOverlay = "Supports/Supports";

    struct SupportPiece
    {
        std::string symbol;
        i32 levels = 0; ///< How tall it is
    };

    /// The pieces of one group that face one way.
    struct SupportSet
    {
        SupportKind kind = kRoundSupport;
        std::vector<SupportPiece> middles; ///< Tallest first, one per height
        std::optional<SupportPiece> foot;
        std::optional<SupportPiece> deep_foot; ///< For ground that is not level
        std::optional<SupportPiece> top;
        std::optional<SupportPiece> cap;
        std::vector<SupportPiece> beams;
        bool stands_on_anything = false; ///< Its columns may stand on any item below, not only on carriers
        bool stands_close = false;       ///< And on track right below, with no level kept clear
    };

    /// Each group's sets, one per way they face.
    using SupportSets = std::map<std::string, std::vector<SupportSet>, std::less<>>;

    /// Item flags of support pieces that the set takes on.
    inline constexpr u32 kStandsOnAnything = 0x4000;
    inline constexpr u32 kStandsClose = 0x8000;

    /// Adds a piece to the set of `group` facing `kind`, by its item type; other types are ignored, and so
    /// is a second piece of a role and height the set already has. `flags` are the piece's item flags.
    void add_support_piece(SupportSets& sets, std::string_view group, u32 type, std::string_view symbol, i32 levels, SupportKind kind = kRoundSupport, u32 flags = 0);

    /// The support sets of the overlay at `path`.
    [[nodiscard]] std::expected<SupportSets, std::string> load_support_sets(const std::filesystem::path& path);

    struct FoundSet
    {
        const SupportSet* set = nullptr;
        ModelTurn turn; ///< To turn the set's pieces by so they face the way asked
    };

    /// The set of `group` for a column facing `kind`: one facing that way, turned when it faces another
    /// diagonal or the other axis. Failing that, the round set, and failing that any set of the group.
    [[nodiscard]] FoundSet find_support_set(const SupportSets& sets, std::string_view group, SupportKind kind);

} // namespace rct3::park::support
