// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/string_table.hpp"

#include "ovl/overlay.hpp"
#include "ovl/text.hpp"

namespace rct3::ui
{

    std::expected<StringTable, std::string> StringTable::load(const std::filesystem::path& path)
    {
        auto overlay = ovl::Overlay::load(path);
        if (!overlay)
            return std::unexpected(std::string(ovl::to_string(overlay.error())));
        StringTable table;
        for (const ovl::Part* part : { &overlay->common(), overlay->unique() })
        {
            if (part == nullptr)
                continue;
            for (ovl::TextItem& item : ovl::read_texts(*overlay, *part))
            {
                if (item.value)
                    table.add(std::string(item.name), std::move(*item.value));
            }
        }
        return table;
    }

    void StringTable::merge(const StringTable& other)
    {
        for (const auto& [key, value] : other.strings_)
            strings_.try_emplace(key, value);
    }

    void StringTable::add(std::string key, std::u16string value)
    {
        strings_.insert_or_assign(std::move(key), std::move(value));
    }

    std::u16string_view StringTable::get(std::string_view key) const
    {
        const auto it = strings_.find(key);
        return it == strings_.end() ? std::u16string_view { } : std::u16string_view { it->second };
    }

    bool StringTable::contains(std::string_view key) const
    {
        return strings_.contains(key);
    }

} // namespace rct3::ui
