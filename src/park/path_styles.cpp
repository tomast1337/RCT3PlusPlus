// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/path_styles.hpp"

#include "strings/ascii.hpp"

#include <algorithm>
#include <format>
#include <system_error>

namespace rct3::park
{

    namespace
    {

        constexpr std::string_view kStubEnding = "_stub.common.ovl";

        constexpr std::size_t kGroundNames = 2;
        constexpr std::size_t kNamesPerPiece = 4;
        /// The longer layout names more pieces after these, which only one style fills.
        constexpr std::size_t kPieces = 19;
        constexpr std::string_view kCommonEnding = ".common.ovl";

        std::int32_t first_cost(const std::vector<ovl::SceneryItem>& items)
        {
            return items.empty() ? 0 : items.front().cost;
        }

        std::string first_group(const std::vector<ovl::SceneryItem>& items)
        {
            const auto named = std::ranges::find_if(items, [](const ovl::SceneryItem& i) { return !i.group.empty(); });
            return named != items.end() ? std::string(named->group) : std::string();
        }

    } // namespace

    PathStyle path_style(const ovl::PathType& type)
    {
        PathStyle style { std::string(type.name), std::string(type.name_text), std::string(type.icon), first_cost(type.items), false, { }, { }, first_group(type.items) };
        for (std::size_t at = kGroundNames; at < type.piece_names.size() && style.pieces.size() < kPieces; at += kNamesPerPiece)
            style.pieces.emplace_back(type.piece_names[at]);
        return style;
    }

    PathStyle queue_style(const ovl::QueueType& type)
    {
        PathStyle style { std::string(type.name), std::string(type.name_text), std::string(type.icon), first_cost(type.items), true, { }, { }, first_group(type.items) };
        for (const std::string_view name : type.piece_names)
            style.pieces.emplace_back(name);
        return style;
    }

    std::vector<PathStyle> path_styles(const ovl::Overlay& overlay)
    {
        std::vector<PathStyle> paths;
        std::vector<PathStyle> queues;
        for (const auto part : { maybe(&overlay.common()), overlay.unique() })
        {
            if (!part)
                continue;
            for (const ovl::PathTypeEntry& e : ovl::read_path_types(overlay, *part))
            {
                if (e.path)
                {
                    // Some stubs leave the type's own name empty; the symbol names the style then.
                    paths.push_back(path_style(*e.path));
                    if (paths.back().name.empty())
                        paths.back().name = std::string(e.name);
                }
            }
            for (const ovl::QueueTypeEntry& e : ovl::read_queue_types(overlay, *part))
            {
                if (e.queue)
                {
                    queues.push_back(queue_style(*e.queue));
                    if (queues.back().name.empty())
                        queues.back().name = std::string(e.name);
                }
            }
        }
        paths.insert(paths.end(), std::make_move_iterator(queues.begin()), std::make_move_iterator(queues.end()));
        return paths;
    }

    bool is_style_overlay(std::string_view file)
    {
        return file.size() > kStubEnding.size() && strings::equals_ignoring_case(file.substr(file.size() - kStubEnding.size()), kStubEnding);
    }

    std::vector<PathStyle> folder_path_styles(const std::filesystem::path& folder)
    {
        std::vector<std::filesystem::path> folders;
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(folder, error))
        {
            if (entry.is_directory(error))
                folders.push_back(entry.path());
        }
        std::ranges::sort(folders);
        std::vector<PathStyle> styles;
        for (const std::filesystem::path& style : folders)
        {
            for (const auto& entry : std::filesystem::directory_iterator(style, error))
            {
                if (!is_style_overlay(entry.path().filename().string()))
                    continue;
                if (const auto overlay = ovl::Overlay::load(entry.path()))
                {
                    std::vector<PathStyle> found = path_styles(*overlay);
                    std::string file = entry.path().filename().string();
                    file.resize(file.size() - kCommonEnding.size());
                    for (PathStyle& s : found)
                    {
                        if (s.name.empty())
                            s.name = style.filename().string();
                        s.overlay = std::format("{}/{}/{}", folder.filename().string(), style.filename().string(), file);
                    }
                    styles.insert(styles.end(), std::make_move_iterator(found.begin()), std::make_move_iterator(found.end()));
                }
                break;
            }
        }
        return styles;
    }

} // namespace rct3::park
