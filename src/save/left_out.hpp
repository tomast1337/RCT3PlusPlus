// SPDX-License-Identifier: GPL-3.0-or-later
//
// What a load of an original save left behind: the classes and fields the save's class table names that no
// loader looked up, told from the file's own schema and a FieldReads log kept while loading. It knows nothing
// about what a class means, beyond a rough sorting of class names into what a player would call them and a list
// of classes and fields left on purpose, because rct3++ builds what they hold from the game's own files or never
// needs it.
#pragma once

#include "core/types.hpp"
#include "save/save_file.hpp"
#include "save/value.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::save
{

    /// A class of the save with objects in it, some or all of whose fields were never looked up.
    struct LeftOutClass
    {
        std::string_view name { };
        usize objects = 0;                   ///< Objects of the class in the file
        std::vector<std::string> fields { }; ///< Never looked up, nested ones as "Outer.Inner", in schema order
        bool untouched = false;              ///< None of its fields was looked up
    };

    /// How many classes or fields a load left unread, left on purpose, or read. A class counts as unread while
    /// any of its fields is, as skipped when the rest of it is all left on purpose. A field is an attribute of a
    /// class, nested ones under a read list or struct apiece, counted once per class however many objects it has.
    struct LeftOutCount
    {
        usize unread = 0;
        usize skipped = 0;
        usize read = 0;
    };

    struct LeftOutTotals
    {
        LeftOutCount classes { };
        LeftOutCount fields { };
    };

    struct LeftOut
    {
        /// The classes with objects some of whose fields were neither looked up nor left on purpose, in class
        /// table order. Classes with no objects or no fields leave nothing behind.
        std::vector<LeftOutClass> classes { };
        LeftOutTotals totals { }; ///< Over every class with objects and fields
    };

    [[nodiscard]] LeftOut left_out(const SaveFile& file, const FieldReads& reads);

    /// Why `field` of `class_name` ("Outer.Inner" when nested) needs no reading, if it is left on purpose.
    [[nodiscard]] std::optional<std::string_view> skip_reason(std::string_view class_name, std::string_view field) noexcept;

    /// What the player would call what a class holds, as a text key of ours: queues, litter, staff jobs and so
    /// on, and other park details for the rest.
    [[nodiscard]] std::string_view left_out_group(std::string_view class_name) noexcept;

    /// The groups of `classes`, each once, in the order left_out_group's table has them.
    [[nodiscard]] std::vector<std::string_view> left_out_groups(const std::vector<LeftOutClass>& classes);

} // namespace rct3::save
