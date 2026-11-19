// SPDX-License-Identifier: GPL-3.0-or-later
//
// The chains moving up lift pieces. A lift piece's chain parts show a chain picture repeated along the track,
// and the picture slides along them as fast as the ride's lift pulls, for as long as park time runs, whether
// the ride is open or not. Each ride's chain has one shift; pieces laid against their section's own way, or
// whose section lays its piece from the far end, take it the other way round.
#pragma once

#include "core/types.hpp"
#include "math/texture_shift.hpp"
#include "park/object/mesh.hpp"
#include "save/model/rides.hpp"

#include <cstdint>
#include <span>

namespace rct3::park
{

    /// How many times the chain picture repeats along a metre of track, as the lift pieces' meshes lay it out.
    inline constexpr f32 kChainRepeatsPerMetre = 4;

    /// The chain parts' runs (see Chains) of every ride's track pieces, by the object carrying each piece, among
    /// `objects` drawn with `models`.
    [[nodiscard]] object::Chains chain_runs(const save::model::RideModel& rides, std::span<const save::model::PlacedObject> objects, const object::ObjectModels& models);

    /// A chain's shift, a share of its picture from 0 up to 1, `seconds` of park time on from `shift` with
    /// its lift pulling at `speed` metres a second.
    [[nodiscard]] f32 chain_shift(f32 shift, f32 speed, f32 seconds);

    /// How far along its picture a chain part of run `run` is moved, with each ride's shift in `shifts` by its
    /// place in the ride list; none for parts of no chain.
    [[nodiscard]] math::TextureShift chain_scroll(std::span<const f32> shifts, i32 run);

} // namespace rct3::park
