// SPDX-License-Identifier: GPL-3.0-or-later
//
// Crowds round a person: an entertainer's audience and a VIP's autograph hunters. Its values
// are in tuning.hpp (Shows, Fans). A guest walking free near the star (alone, or leading their party, which
// comes too) may stop, step across the path to a place on a ring round them and watch. An entertainer looks
// for guests now and then and, found, puts on a show: a few moves in turn, more and fancier the better trained
// they are, a poor one (miserable or lazy) falling over and ending dejected. A good show ends with the crowd
// clapping, happier; a poor one with them shaking their heads. A VIP looks for fans now and then; they stand
// round them till signed for, one at a time, and a VIP who minds hunters loses some patience for each.
#pragma once

#include "random/generator.hpp"
#include "simulation/components.hpp"

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace rct3::simulation
{

    struct SimWorld;

    /// The chance an entertainer starts a show when they find guests near: higher trained, happy and keen.
    [[nodiscard]] float show_chance(const Staff& staff);

    /// Whether a show they put on now is a poor one: they are miserable or lazy.
    [[nodiscard]] bool poor_show(const Staff& staff);

    /// How good a show they put on now is, 0 to 3: their training, a level less when miserable and another when
    /// lazy.
    [[nodiscard]] std::int32_t show_quality(const Staff& staff);

    /// The moves of a show of `quality` (0 to 3), poor or not, drawn with `dice`. A good show has one move at its
    /// height, the best dance its quality allows (a wave untrained), and the others from those it allows, more
    /// on offer the better it is. A poor one falls over somewhere and ends dejected.
    [[nodiscard]] std::vector<ShowMove> show_moves(std::int32_t quality, bool poor, random::RandomState& dice);

    /// How long `moves` take in turn.
    [[nodiscard]] float show_seconds(std::span<const ShowMove> moves);

    /// The move of `show` they make now, and the seconds into it; the last once the show is through.
    [[nodiscard]] std::pair<ShowMove, float> show_move_at(const Show& show);

    /// One tick of the crowds: entertainers' shows go on or start, fans gather round VIPs for autographs, and
    /// those watching step to their place round the star; nothing without paths. Run as part of the "Tidy"
    /// phase, after the staff.
    void step_crowds(SimWorld& world);

} // namespace rct3::simulation
