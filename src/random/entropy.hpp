// SPDX-License-Identifier: GPL-3.0-or-later
//
// A fresh seed for normal play, when no seed is asked for. The clock alone repeats when two runs start in
// the same tick, so the seed mixes several sources that differ between runs and machines: the operating
// system's random device, the wall clock and the steady clock in nanoseconds, the process and thread ids,
// and where the stack and the program sit in memory (which address-space randomisation moves). Any source
// that is missing or fails adds nothing; the rest still count.
#pragma once

#include "random/generator.hpp"

namespace rct3::random
{

    [[nodiscard]] Seed fresh_seed();

} // namespace rct3::random
