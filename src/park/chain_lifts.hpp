// SPDX-License-Identifier: GPL-3.0-or-later
//
// The chains moving up lift pieces. A lift piece's chain parts show a chain picture repeated along the track,
// and the picture slides along them as fast as the ride's lift pulls, for as long as park time runs, whether
// the ride is open or not. Each ride's chain has one shift; pieces laid against their section's own way, or
// whose section lays its piece from the far end, take it the other way round.
#pragma once

#include "math/texture_shift.hpp"
#include "park/object_mesh.hpp"
#include "save/model/rides.hpp"

#include <cstdint>
#include <span>

namespace rct3::park
{

    /// How many times the chain picture repeats along a metre of track, as the lift pieces' meshes lay it out.
    inline constexpr float kChainRepeatsPerMetre = 4;

    /// The chain parts' runs (see Chains) of every ride's track pieces, by the object carrying each piece, among
    /// `objects` drawn with `models`.
    [[nodiscard]] Chains chain_runs(const save::RideModel& rides, std::span<const save::PlacedObject> objects, const ObjectModels& models);

    /// A chain's shift, a share of its picture from 0 up to 1, `seconds` of park time on from `shift` with
    /// its lift pulling at `speed` metres a second.
    [[nodiscard]] float chain_shift(float shift, float speed, float seconds);

    /// How far along its picture a chain part of run `run` is moved, with each ride's shift in `shifts` by its
    /// place in the ride list; none for parts of no chain.
    [[nodiscard]] TextureShift chain_scroll(std::span<const float> shifts, std::int32_t run);

} // namespace rct3::park
