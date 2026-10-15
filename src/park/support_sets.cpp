// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/support_sets.hpp"

#include "ovl/overlay.hpp"
#include "ovl/scenery_item.hpp"

#include <algorithm>

namespace rct3::park
{

    namespace
    {

        constexpr SupportKind kLastDiagonal = kFirstDiagonal + 3;

        bool diagonal(SupportKind k)
        {
            return k >= kFirstDiagonal && k <= kLastDiagonal;
        }

        bool along_axis(SupportKind k)
        {
            return k == kAlongX || k == kAlongZ;
        }

        void keep_first(std::optional<SupportPiece>& slot, SupportPiece piece)
        {
            if (!slot)
                slot = std::move(piece);
        }

    } // namespace

    void add_support_piece(SupportSets& sets, std::string_view group, std::uint32_t type, std::string_view symbol, std::int32_t levels, SupportKind kind, std::uint32_t flags)
    {
        if (group.empty() || symbol.empty() || levels <= 0)
            return;
        if (type < kSupportMiddleType || type > kSupportCapType)
            return;
        auto found = sets.find(group);
        if (found == sets.end())
            found = sets.emplace(std::string(group), std::vector<SupportSet> { }).first;
        auto& kinds = found->second;
        auto set = std::ranges::find(kinds, kind, &SupportSet::kind);
        if (set == kinds.end())
        {
            SupportSet added;
            added.kind = kind;
            set = kinds.insert(kinds.end(), std::move(added));
        }
        SupportSet& s = *set;
        s.stands_on_anything = s.stands_on_anything || (flags & (kStandsOnAnything | kStandsClose)) != 0;
        s.stands_close = s.stands_close || (flags & kStandsClose) != 0;
        SupportPiece piece { std::string(symbol), levels };
        switch (type)
        {
            case kSupportTopType:
                keep_first(s.top, std::move(piece));
                return;
            case kSupportFootType:
                keep_first(s.foot, std::move(piece));
                return;
            case kSupportDeepFootType:
                keep_first(s.deep_foot, std::move(piece));
                return;
            case kSupportCapType:
                keep_first(s.cap, std::move(piece));
                return;
            case kSupportBeamType:
                s.beams.push_back(std::move(piece));
                return;
            default:
                break;
        }
        auto& middles = s.middles;
        if (std::ranges::any_of(middles, [&](const SupportPiece& m) { return m.levels == levels; }))
            return;
        middles.insert(std::ranges::find_if(middles, [&](const SupportPiece& m) { return m.levels < levels; }), std::move(piece));
    }

    std::expected<SupportSets, std::string> load_support_sets(const std::filesystem::path& path)
    {
        auto overlay = ovl::Overlay::load(path);
        if (!overlay)
            return std::unexpected(std::string(ovl::to_string(overlay.error())));
        SupportSets sets;
        for (const ovl::Part* part : { &overlay->common(), overlay->unique() })
        {
            if (part == nullptr)
                continue;
            for (const ovl::SceneryItemEntry& entry : ovl::read_scenery_items(*overlay, *part))
            {
                if (!entry.item || entry.item->squares.empty())
                    continue;
                const auto& squares = entry.item->squares;
                const std::int32_t levels = std::ranges::max(squares, { }, &ovl::SceneryItemSquare::max_height).max_height - std::ranges::min(squares, { }, &ovl::SceneryItemSquare::min_height).min_height;
                add_support_piece(sets, entry.item->group, entry.item->type, entry.name, levels, squares.front().support_kind, entry.item->flags);
            }
        }
        return sets;
    }

    FoundSet find_support_set(const SupportSets& sets, std::string_view group, SupportKind kind)
    {
        const SupportSet* round = nullptr;
        const SupportSet* any = nullptr;
        const auto found = sets.find(group);
        if (found == sets.end())
            return { };
        for (const SupportSet& s : found->second)
        {
            if (s.kind == kind)
                return { &s, 0 };
            if (diagonal(s.kind) && diagonal(kind))
                return { &s, static_cast<std::int32_t>((kind - s.kind + 4) % 4) };
            if (along_axis(s.kind) && along_axis(kind))
                return { &s, 1 };
            if (s.kind == kRoundSupport)
                round = &s;
            if (any == nullptr)
                any = &s;
        }
        return { round != nullptr ? round : any, 0 };
    }

} // namespace rct3::park
