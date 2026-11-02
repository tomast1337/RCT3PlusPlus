// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/crowds.hpp"

#include "simulation/staff_care.hpp"
#include "simulation/tuning.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace rct3::simulation
{

    float show_chance(const Staff& staff)
    {
        const auto& t = kTuning.shows;
        const float keen = t.keen.value + t.keen_per_training.value * static_cast<float>(staff.training) + (staff.happiness.value - 0.5f) / 2 - staff.laziness.value / 2;
        return std::clamp(keen, 0.05f, 1.0f);
    }

    bool poor_show(const Staff& staff)
    {
        return staff.happiness.value < kTuning.staff.miserable.value || staff.laziness.value >= kTuning.shows.poor_lazy.value;
    }

    std::int32_t show_quality(const Staff& staff)
    {
        const std::int32_t less = (staff.happiness.value < kTuning.staff.miserable.value ? 1 : 0) + (staff.laziness.value >= kTuning.staff.tired.value ? 1 : 0);
        return std::clamp(staff.training - less, 0, kMostTraining);
    }

    std::vector<ShowMove> show_moves(std::int32_t quality, bool poor, random::RandomState& dice)
    {
        using enum ShowMove;
        quality = std::clamp(quality, 0, kMostTraining);
        const auto count = static_cast<std::size_t>(kTuning.shows.moves + quality);
        std::vector<ShowMove> moves;
        if (poor)
        {
            constexpr std::array kTired { Idle, IdleTwo, Wave };
            const std::size_t fall = random::below(dice, count - 1);
            for (std::size_t i = 0; moves.size() < count; ++i)
            {
                if (i == fall)
                {
                    moves.push_back(FallOver);
                    moves.push_back(GetUp);
                }
                else
                    moves.push_back(kTired[random::below(dice, kTired.size())]);
            }
            moves.resize(count);
            moves.push_back(Dejected);
            return moves;
        }
        // Better shows have more moves on offer, the fancier last.
        constexpr std::array kOffered { Wave, Idle, DanceOne, IdleTwo, DanceTwo, DanceThree };
        constexpr std::array kHeight { Wave, DanceOne, DanceTwo, DanceThree };
        const std::size_t height = random::below(dice, count);
        for (std::size_t i = 0; i < count; ++i)
            moves.push_back(i == height ? kHeight[static_cast<std::size_t>(quality)] : kOffered[random::below(dice, static_cast<std::size_t>(quality) + 3)]);
        return moves;
    }

    float show_seconds(std::span<const ShowMove> moves)
    {
        float seconds = 0;
        for (const ShowMove m : moves)
            seconds += kTuning.shows.move_length[std::to_underlying(m)].value;
        return seconds;
    }

    std::pair<ShowMove, float> show_move_at(const Show& show)
    {
        float into = show.into.value;
        for (const ShowMove m : show.moves)
        {
            const float length = kTuning.shows.move_length[std::to_underlying(m)].value;
            if (into < length || &m == &show.moves.back())
                return { m, std::min(into, length) };
            into -= length;
        }
        return { ShowMove::Idle, 0.0f };
    }

} // namespace rct3::simulation
