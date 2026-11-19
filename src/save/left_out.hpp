// SPDX-License-Identifier: GPL-3.0-or-later
//
// What a load of an original save left behind: the classes and fields the save's class table names that no
// loader looked up, told from the file's own schema and a FieldReads log kept while loading. It knows nothing
// about what a class means, beyond a rough sorting of class names into what a player would call them.
#pragma once

#include "core/types.hpp"
#include "save/save_file.hpp"
#include "save/value.hpp"

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

    /// The classes with objects whose fields were not all looked up, in class table order. Classes with no
    /// objects or no fields leave nothing behind.
    [[nodiscard]] std::vector<LeftOutClass> left_out(const SaveFile& file, const FieldReads& reads);

    /// What the player would call what a class holds, as a text key of ours: queues, litter, staff jobs and so
    /// on, and other park details for the rest.
    [[nodiscard]] std::string_view left_out_group(std::string_view class_name) noexcept;

    /// The groups of `classes`, each once, in the order left_out_group's table has them.
    [[nodiscard]] std::vector<std::string_view> left_out_groups(const std::vector<LeftOutClass>& classes);

} // namespace rct3::save
