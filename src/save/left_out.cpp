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

        /// A class, or one field of it ("Outer.Inner" when nested, or a list or struct with all its parts), that
        /// a load leaves on purpose. Empty field: the whole class.
        struct Skip
        {
            std::string_view class_name;
            std::string_view field;
            std::string_view reason;
        };

        constexpr std::array kSkips {
            Skip { "FlexiColourGroupCache", { }, "a cache of colour groups, made again as the park's objects load" },
            Skip { "GUI2_DatDesc", { }, "the summary and picture the load window shows for the file; a save writes its own" },
            Skip { "GUI2_CurrentDatDesc", { }, "points at the file's load window summary" },
            Skip { "RideParticleManager", { }, "the list of what gives off particles now, made again from the rides, benches and waterfalls" },
            Skip { "PathManager", "TileList", "empty in every save; path pieces are objects of their own" },
            Skip { "GuestInjectionPoint", "\"SceneryItem\"", "points back at the placed item that carries it, read with the item" },
            Skip { "ParkEntranceBehaviour", "\"SceneryItem\"", "points back at the placed item that carries it, read with the item" },
            Skip { "ParkInspectorManager", "ParkInspectorRef", "points at the park's one inspector, read with the staff" },
            Skip { "AIDDatabaseEntry", "ATTRTYPE", "the attraction's kind, which its own game file gives" },
            Skip { "AIDDatabaseEntry", "HaveAddedIAs", "set once the list of show tricks is filled in, which the list itself tells" },
            Skip { "CEDDatabaseEntry", "SymbolName", "a stall extra's name, looked up through the extras stalls sell; the rest list the install" },
            Skip { "PPGDatabaseEntry", "SymbolNameIndex", "where the entry's own name sits in a table of the same names" },
            Skip { "PPGStringTable", { }, "a table of the names the part group entries already hold" },
            Skip { "StaffSettings", "costumeOverlayName", "the staff uniform set, the standard one in every save seen" },
            Skip { "PathTile", "bool", "false in every save seen" },
            Skip { "PathQueue", "bool", "false in every save seen" },
            Skip { "PathGround", "bool", "false in every save seen" },
            Skip { "PathFlying", "bool", "false in every save seen" },
        };

        /// Sorts the fields of `attributes` into read, skipped and unread, naming the unread ones under `prefix`
        /// in `left`; returns whether any was read.
        bool sort_fields(const std::vector<Attribute>& attributes, const FieldReads& reads, const std::string& prefix, LeftOutClass& left, LeftOutCount& fields)
        {
            bool any = false;
            for (const Attribute& a : attributes)
            {
                const std::string name = prefix.empty() ? std::string(a.name) : std::format("{}.{}", prefix, a.name);
                if (!reads.was_read(a.name))
                {
                    if (skip_reason(left.name, name))
                        ++fields.skipped;
                    else
                    {
                        left.fields.push_back(name);
                        ++fields.unread;
                    }
                    continue;
                }
                any = true;
                ++fields.read;
                // A list or struct read may still leave some of its parts. The class table's depth is bounded.
                if (a.children.size() > 1)
                    sort_fields(a.children, reads, name, left, fields);
            }
            return any;
        }
    } // namespace

    LeftOut left_out(const SaveFile& file, const FieldReads& reads)
    {
        std::vector<usize> objects(file.classes.size());
        for (const Object& o : file.objects)
            if (o.class_index < objects.size())
                ++objects[o.class_index];
        LeftOut out;
        for (usize i = 0; i < file.classes.size(); ++i)
        {
            const Class& c = file.classes[i];
            if (objects[i] == 0 || c.attributes.empty())
                continue;
            LeftOutClass left { .name = c.name, .objects = objects[i] };
            LeftOutCount fields;
            left.untouched = !sort_fields(c.attributes, reads, { }, left, fields);
            out.totals.fields.unread += fields.unread;
            out.totals.fields.skipped += fields.skipped;
            out.totals.fields.read += fields.read;
            if (fields.unread > 0)
            {
                ++out.totals.classes.unread;
                out.classes.push_back(std::move(left));
            }
            else if (fields.skipped > 0)
                ++out.totals.classes.skipped;
            else
                ++out.totals.classes.read;
        }
        return out;
    }

    std::optional<std::string_view> skip_reason(std::string_view class_name, std::string_view field) noexcept
    {
        for (const Skip& s : kSkips)
            if (s.class_name == class_name && (s.field.empty() || field == s.field || (field.starts_with(s.field) && field.substr(s.field.size()).starts_with('.'))))
                return s.reason;
        return std::nullopt;
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
