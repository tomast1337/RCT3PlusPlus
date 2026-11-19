// SPDX-License-Identifier: GPL-3.0-or-later
#include "save/left_out.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <array>
#include <format>

namespace rct3::save
{

    namespace
    {
        struct Group
        {
            std::string_view key;
            std::array<std::string_view, 12> parts { }; ///< A class whose name holds one of these is in the group
        };

        // First match wins, so a queue's entertainer is told as a queue and a ride's guests as the ride.
        constexpr std::array kGroups {
            Group { "Rct3pp_LeftOutQueues", { "Queue" } },
            Group { "Rct3pp_LeftOutLitter", { "Litter" } },
            Group { "Rct3pp_LeftOutStaff", { "Handyman", "Mechanic", "SecurityGuard", "Entertainer", "Vendor", "AnimalKeeper", "LifeGuard", "Lifeguard", "ParkInspector", "Staff" } },
            Group { "Rct3pp_LeftOutPools", { "Pool", "ChangingRoom" } },
            Group { "Rct3pp_LeftOutAnimals", { "WildAnimal", "WAS_", "Trough", "AnimalShelter", "Enclosure", "Duck", "Seagull", "Fish" } },
            Group { "Rct3pp_LeftOutShows", { "Firework", "Laser", "Particle", "WaterJet", "JFDisplay", "Show", "Fountain" } },
            Group { "Rct3pp_LeftOutRides", { "Ride", "Track", "SpecialAttraction" } },
            Group { "Rct3pp_LeftOutStalls", { "Stall", "Shop", "CarriedItem" } },
            Group { "Rct3pp_LeftOutGuests", { "Guest", "Person" } },
        };
        constexpr std::string_view kOther = "Rct3pp_LeftOutOther";

        /// Adds the fields of `attributes` not looked up to `out`, named under `prefix`; returns whether any was.
        bool unread(const std::vector<Attribute>& attributes, const FieldReads& reads, const std::string& prefix, std::vector<std::string>& out)
        {
            bool any = false;
            for (const Attribute& a : attributes)
            {
                const std::string name = prefix.empty() ? std::string(a.name) : std::format("{}.{}", prefix, a.name);
                if (!reads.was_read(a.name))
                {
                    out.push_back(name);
                    continue;
                }
                any = true;
                // A list or struct read may still leave some of its parts. The class table's depth is bounded.
                if (a.children.size() > 1)
                    unread(a.children, reads, name, out);
            }
            return any;
        }
    } // namespace

    std::vector<LeftOutClass> left_out(const SaveFile& file, const FieldReads& reads)
    {
        std::vector<usize> objects(file.classes.size());
        for (const Object& o : file.objects)
            if (o.class_index < objects.size())
                ++objects[o.class_index];
        std::vector<LeftOutClass> out;
        for (usize i = 0; i < file.classes.size(); ++i)
        {
            const Class& c = file.classes[i];
            if (objects[i] == 0 || c.attributes.empty())
                continue;
            LeftOutClass left { .name = c.name, .objects = objects[i] };
            left.untouched = !unread(c.attributes, reads, { }, left.fields);
            if (!left.fields.empty())
                out.push_back(std::move(left));
        }
        return out;
    }

    std::string_view left_out_group(std::string_view class_name) noexcept
    {
        for (const Group& g : kGroups)
            if (std::ranges::any_of(g.parts, [&](std::string_view part) { return !part.empty() && class_name.contains(part); }))
                return g.key;
        return kOther;
    }

    std::vector<std::string_view> left_out_groups(const std::vector<LeftOutClass>& classes)
    {
        std::vector<std::string_view> out;
        for (const Group& g : kGroups)
            if (std::ranges::any_of(classes, [&](const LeftOutClass& c) { return left_out_group(c.name) == g.key; }))
                out.push_back(g.key);
        if (std::ranges::any_of(classes, [](const LeftOutClass& c) { return left_out_group(c.name) == kOther; }))
            out.push_back(kOther);
        return out;
    }

} // namespace rct3::save
