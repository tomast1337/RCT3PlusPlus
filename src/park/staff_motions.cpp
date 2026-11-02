// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/staff_motions.hpp"

namespace rct3::park
{

    WorkLook work_look(simulation::StaffJob job, simulation::Breakdown breakdown)
    {
        using simulation::Breakdown;
        using simulation::StaffJob;
        switch (job)
        {
            case StaffJob::None:
                return { };
            case StaffJob::Sweep:
                return { .loop = "sweep", .tool = "Brush" };
            case StaffJob::EmptyBin:
                return { .loop = "emptybin" };
            case StaffJob::Mend:
                return { .loop = "clean", .tool = "Spunge" }; // Wiping it down with a sponge
            case StaffJob::Water:
                return { .loop = "watergarden", .tool = "WaterCan" };
            case StaffJob::Inspect:
                return { .loop = "inspectride" };
            case StaffJob::Watch:
                return { .start = "swingtruncheon", .loop = "talkradio" };
            case StaffJob::Rest:
                return { .start = "sitbenchin", .loop = "sitbenchloop", .finish = "sitbenchout" };
            case StaffJob::Fix:
                break;
        }
        // Each breakdown worked on as its kind suggests: the restraints, the doors, the brakes, the train.
        switch (breakdown)
        {
            case Breakdown::RestraintsFailToOpen:
            case Breakdown::RestraintsFailToClose:
                return { .loop = "restraintfailopenclose" };
            case Breakdown::DoorsFailToOpen:
            case Breakdown::DoorsFailToClose:
                return { .loop = "fixdooropenclose" };
            case Breakdown::StationBrakesFail:
                return { .start = "fixbrakesin", .loop = "fixbrakesloop", .finish = "fixbrakesout" };
            case Breakdown::TrainStuckLeavingStation:
                return { .start = "fixbrokentrainin", .loop = "fixbrokentrainloop", .finish = "fixbrokentrainout" };
            case Breakdown::EmergencyStop:
                return { .start = "fixvehicle1in", .loop = "fixvehicle1loop", .finish = "fixvehicle1out" };
            case Breakdown::ControlFailure:
                return { .loop = "electricfiddle" };
            case Breakdown::None:
                break;
        }
        return { .start = "fixvehicle2in", .loop = "fixvehicle2loop", .finish = "fixvehicle2out" };
    }

    std::string_view keeper_motion(simulation::KeeperJob job, bool walking)
    {
        const bool container = job == simulation::KeeperJob::Filling;
        if (walking)
            return container ? "walkwithcontainer" : "walkwithshovel";
        switch (job)
        {
            case simulation::KeeperJob::Filling:
                return "containerloop";
            case simulation::KeeperJob::Clearing:
                return "shovelloop";
            default:
                return "standwithshovel";
        }
    }

} // namespace rct3::park
