// SPDX-License-Identifier: GPL-3.0-or-later
//
// Staff at work, each tick: paid as each week ends; at their posts, content or not; and on the paths, where
// mechanics are sent to broken rides and those due an inspection, handymen sweep, mend, empty bins and water
// beds, guards keep watch, inspectors go on their rounds, and all of them keep to their patrols.
#pragma once

namespace rct3::simulation
{
    struct SimWorld;
}

namespace rct3::simulation::staff
{

    /// One tick of the staff's work, after the guests have walked and dropped their litter. Run as part of the
    /// "Tidy" phase.
    void step_work(SimWorld& world);

} // namespace rct3::simulation::staff
