// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track_choice.hpp"

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <tuple>
#include <utility>

namespace rct3::park
{

    namespace
    {
        /// A plain candidate as the picks see it.
        struct Offer
        {
            int curve = 0;
            int slope = 0;
            int bank = 0;
            std::uint8_t toggles = 0;
            LaidPiece laid;
        };

        std::vector<Offer> offers(std::span<const PieceShape> pieces, std::span<const LaidPiece> candidates)
        {
            std::vector<Offer> out;
            for (const LaidPiece& c : candidates)
            {
                if (c.index >= pieces.size())
                    continue;
                const PieceShape& p = pieces[c.index];
                if (p.use != PieceUse::Plain)
                    continue;
                const PieceEnd exit = laid_exit(p, c.reversed);
                out.push_back({ laid_curve(p, c.reversed), exit.slope, exit.bank, p.toggles, c });
            }
            return out;
        }

        bool contains(std::span<const LaidPiece> candidates, const LaidPiece& laid)
        {
            return std::ranges::any_of(candidates, [&](const LaidPiece& c) { return c.index == laid.index && c.reversed == laid.reversed; });
        }

        bool matches(const Offer& o, const TrackChoice& c)
        {
            return o.curve == c.curve && o.slope == c.slope && o.bank == c.bank;
        }

        /// How many toggles an offer has other than asked.
        int toggles_apart(const Offer& o, const TrackChoice& c)
        {
            return std::popcount(static_cast<unsigned>(o.toggles ^ c.toggles));
        }

        /// How far an offer is from the picks: steps on each pick, changed toggles counting less than any step,
        /// then picks nearer level and straight first.
        auto distance(const Offer& o, const TrackChoice& c)
        {
            const int steps = std::abs(o.curve - c.curve) + std::abs(o.slope - c.slope) + std::abs(o.bank - c.bank);
            return std::make_tuple(steps, toggles_apart(o, c), std::abs(o.curve) + std::abs(o.slope) + std::abs(o.bank));
        }

        /// `choice` moved onto the nearest of `all` that `keep` accepts; unchanged when there is none.
        template<typename Keep> TrackChoice nearest(const std::vector<Offer>& all, TrackChoice choice, Keep&& keep)
        {
            if (std::ranges::any_of(all, [&](const Offer& o) { return matches(o, choice); }))
                return choice;
            const Offer* best = nullptr;
            for (const Offer& o : all)
            {
                if (keep(o) && (best == nullptr || distance(o, choice) < distance(*best, choice)))
                    best = &o;
            }
            if (best != nullptr)
            {
                choice.curve = best->curve;
                choice.slope = best->slope;
                choice.bank = best->bank;
            }
            return choice;
        }
    } // namespace

    ChoiceOptions choice_options(std::span<const PieceShape> pieces, std::span<const LaidPiece> candidates, const TrackChoice& choice)
    {
        ChoiceOptions o;
        for (const Offer& offer : offers(pieces, candidates))
        {
            o.curves.at(curve_index(offer.curve)) = true;
            o.slopes.at(slope_index(offer.slope)) = true;
            o.banks.at(bank_index(offer.bank)) = true;
            o.any_toggles |= offer.toggles;
            if (matches(offer, choice))
                o.toggles |= offer.toggles;
        }
        // Stations first, then the rest; a piece that fits both ways is offered as drawn.
        for (const PieceUse use : { PieceUse::Station, PieceUse::Special })
        {
            for (const LaidPiece& c : candidates)
            {
                if (c.index >= pieces.size() || pieces[c.index].use != use)
                    continue;
                if (c.reversed && contains(candidates, { c.index, false }))
                    continue;
                o.named.push_back(c);
            }
        }
        return o;
    }

    std::optional<LaidPiece> chosen_piece(std::span<const PieceShape> pieces, std::span<const LaidPiece> candidates, const TrackChoice& choice)
    {
        if (choice.named)
            return contains(candidates, *choice.named) ? choice.named : std::nullopt;
        const Offer* best = nullptr;
        for (const Offer& o : offers(pieces, candidates))
        {
            if (matches(o, choice) && (best == nullptr || std::pair { toggles_apart(o, choice), o.laid.reversed } < std::pair { toggles_apart(*best, choice), best->laid.reversed }))
                best = &o;
        }
        return best != nullptr ? std::optional { best->laid } : std::nullopt;
    }

    TrackChoice pick_value(std::span<const PieceShape> pieces, std::span<const LaidPiece> candidates, TrackChoice choice, ChoiceAxis axis, int value)
    {
        choice.named.reset();
        const auto on_axis = [axis](const auto& picks) -> int {
            switch (axis)
            {
                case ChoiceAxis::Slope:
                    return picks.slope;
                case ChoiceAxis::Bank:
                    return picks.bank;
                case ChoiceAxis::Curve:
                    break;
            }
            return picks.curve;
        };
        switch (axis)
        {
            case ChoiceAxis::Curve:
                choice.curve = value;
                break;
            case ChoiceAxis::Slope:
                choice.slope = value;
                break;
            case ChoiceAxis::Bank:
                choice.bank = value;
                break;
        }
        return nearest(offers(pieces, candidates), choice, [&](const Offer& o) { return on_axis(o) == value; });
    }

    TrackChoice pick_toggle(std::span<const PieceShape> pieces, std::span<const LaidPiece> candidates, TrackChoice choice, PieceToggle toggle)
    {
        const std::uint8_t bit = toggle_bit(toggle);
        const std::uint8_t wanted = choice.toggles ^ bit;
        const std::vector<Offer> all = offers(pieces, candidates);
        if (std::ranges::any_of(all, [&](const Offer& o) { return matches(o, choice) && (o.toggles & bit) == (wanted & bit); }))
            choice.toggles = wanted;
        return choice;
    }

    TrackChoice settle_choice(std::span<const PieceShape> pieces, std::span<const LaidPiece> candidates, TrackChoice choice)
    {
        if (choice.named && !contains(candidates, *choice.named))
            choice.named.reset();
        const std::vector<Offer> all = offers(pieces, candidates);
        // With no plain piece to pick, as at a tower's base, the first station or special piece is picked.
        if (all.empty() && !choice.named)
        {
            const std::vector<LaidPiece> named = choice_options(pieces, candidates, choice).named;
            if (!named.empty())
                choice.named = named.front();
        }
        return nearest(all, choice, [](const Offer&) { return true; });
    }

} // namespace rct3::park
