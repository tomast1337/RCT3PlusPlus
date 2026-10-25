// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/carried_goods.hpp"

#include "core/maybe.hpp"
#include "ovl/carried_item.hpp"
#include "strings/ascii.hpp"

#include <algorithm>
#include <format>
#include <system_error>

namespace rct3::park
{

    namespace
    {
        constexpr std::string_view kCommonEnding = ".common.ovl";
    } // namespace

    void add_carried_kinds(const ovl::Overlay& overlay, CarriedKinds& kinds)
    {
        for (const auto part : { maybe(&overlay.common()), overlay.unique() })
        {
            if (!part)
                continue;
            for (const ovl::CarriedItemEntry& e : ovl::read_carried_items(overlay, *part))
            {
                if (!e.item)
                    continue;
                CarriedKind k { std::format("{}:cid", e.name), std::string(e.item->name_text), std::string(e.item->icon), { }, !e.item->texture.empty() };
                for (const std::string_view extra : e.item->extras)
                    k.extras.push_back(std::format("{}:ced", extra));
                kinds.items.push_back(std::move(k));
            }
            for (const ovl::CarriedItemExtraEntry& e : ovl::read_carried_item_extras(overlay, *part))
            {
                if (e.extra)
                    kinds.extras.push_back({ std::format("{}:ced", e.name), std::string(e.extra->name_text), std::string(e.extra->icon), { }, false });
            }
        }
    }

    std::vector<std::filesystem::path> carried_overlays(const std::filesystem::path& folder)
    {
        std::vector<std::filesystem::path> files;
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(folder, error))
        {
            const std::string name = entry.path().filename().string();
            if (name.size() > kCommonEnding.size() && strings::equals_ignoring_case(std::string_view(name).substr(name.size() - kCommonEnding.size()), kCommonEnding))
                files.push_back(entry.path());
        }
        std::ranges::sort(files);
        return files;
    }

} // namespace rct3::park
