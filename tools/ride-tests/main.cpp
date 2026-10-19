// SPDX-License-Identifier: GPL-3.0-or-later
//
// ride-tests: the test results of each rated ride in saved parks, beside the ratings the park gave it, as CSV.
//
//   ride-tests SAVE...
//
// One row per tracked ride with a valid test lap and valid ratings. tools/ratings-fit/fit.py fits the ride
// rating model's weights to these rows.
#include "save/model/rides.hpp"
#include "save/save_file.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

using namespace rct3;

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: ride-tests SAVE...\n");
        return 2;
    }
    std::printf("save,type,excitement,intensity,nausea,max_speed,average_speed,ride_time,ride_length,max_positive_vertical,max_negative_vertical,max_lateral,air_time,drop_count,highest_drop,inversion_count,lift_hill_count\n");
    int failed = 0;
    for (int i = 1; i < argc; ++i)
    {
        std::ifstream in(argv[i], std::ios::binary);
        const std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const auto file = save::read_save(std::as_bytes(std::span(raw)), { .verify_checksum = false });
        if (!file)
        {
            std::fprintf(stderr, "%s: %s\n", argv[i], raw.empty() ? "cannot read" : save::to_string(file.error()));
            ++failed;
            continue;
        }
        const save::ObjectIndex index(*file);
        const auto rides = save::load_rides(*file, index);
        if (!rides)
        {
            std::fprintf(stderr, "%s: cannot load rides\n", argv[i]);
            ++failed;
            continue;
        }
        std::string name = argv[i];
        name = name.substr(name.find_last_of('/') + 1);
        for (const save::Ride& ride : rides->rides)
        {
            const auto& t = ride.stats.last_lap;
            if (ride.kind != save::RideKind::Tracked || !ride.assessment.valid || !t || t->max_speed <= 0)
                continue;
            std::printf(
                "\"%s\",\"%s\",%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%d,%g,%d,%d\n", name.c_str(), ride.base.symbol.c_str(), static_cast<double>(ride.assessment.excitement), static_cast<double>(ride.assessment.intensity), static_cast<double>(ride.assessment.nausea), static_cast<double>(t->max_speed), static_cast<double>(t->average_speed), static_cast<double>(t->ride_time), static_cast<double>(t->ride_length), static_cast<double>(t->max_positive_vertical), static_cast<double>(t->max_negative_vertical),
                static_cast<double>(t->max_lateral), static_cast<double>(t->air_time), t->drop_count, static_cast<double>(t->highest_drop), t->inversion_count, t->lift_hill_count);
        }
    }
    return failed == argc - 1 ? 1 : 0;
}
