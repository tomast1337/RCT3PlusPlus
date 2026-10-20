// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/asset_index.hpp"

#include "ovl/overlay.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace rct3::park
{

    namespace
    {

        constexpr std::string_view kCommonEnding = ".common.ovl";

        std::string lower(std::string_view s)
        {
            std::string out(s);
            std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return out;
        }

        std::string forward_slashes(std::string_view s)
        {
            std::string out(s);
            std::ranges::replace(out, '\\', '/');
            return out;
        }

        /// Characters two overlay paths share from the start, compared without case.
        std::size_t shared_start(std::string_view a, std::string_view b)
        {
            std::size_t n = 0;
            while (n < a.size() && n < b.size() && std::tolower(static_cast<unsigned char>(a[n])) == std::tolower(static_cast<unsigned char>(b[n])))
                ++n;
            return n;
        }

    } // namespace

    std::optional<std::string> referenced_overlay(std::string_view from, std::string_view reference)
    {
        std::vector<std::string_view> steps;
        const auto walk = [&](std::string_view path, bool keep_last) {
            while (!path.empty())
            {
                const std::size_t cut = path.find_first_of("/\\");
                const std::string_view step = path.substr(0, cut);
                path = cut == std::string_view::npos ? std::string_view { } : path.substr(cut + 1);
                if (!keep_last && cut == std::string_view::npos)
                    break; // The file's own name: references start from its folder
                if (step.empty() || step == ".")
                    continue;
                if (step == "..")
                {
                    if (steps.empty())
                        return false;
                    steps.pop_back();
                    continue;
                }
                steps.push_back(step);
            }
            return true;
        };
        if (reference.empty() || !walk(from, false) || !walk(reference, true) || steps.empty())
            return std::nullopt;
        std::string out;
        for (const std::string_view step : steps)
        {
            if (!out.empty())
                out += '/';
            out += step;
        }
        return out;
    }

    AssetIndex AssetIndex::scan(const std::filesystem::path& root, std::span<const std::string_view> folders)
    {
        AssetIndex index;
        index.root_ = root;
        for (const std::string_view folder : folders)
        {
            std::error_code error;
            for (auto it = std::filesystem::recursive_directory_iterator(root / folder, error); !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
            {
                const std::string name = it->path().filename().string();
                if (it->is_regular_file(error) && name.size() > kCommonEnding.size() && name.ends_with(kCommonEnding))
                    index.add_file(it->path());
            }
        }
        return index;
    }

    bool AssetIndex::add_file(const std::filesystem::path& file)
    {
        auto overlay = ovl::Overlay::load(file);
        if (!overlay)
            return false;
        std::string path = file.lexically_relative(root_).generic_string();
        if (path.size() <= kCommonEnding.size() || path.starts_with(".."))
            return false;
        path.resize(path.size() - kCommonEnding.size());
        for (const auto part : { maybe(&overlay->common()), overlay->unique() })
        {
            if (!part)
                continue;
            for (const ovl::Symbol& symbol : part->symbols)
            {
                if (symbol.is_pointer && !symbol.name.empty())
                    add(symbol.name, path);
            }
            for (const std::string& reference : part->file.header.external_refs)
            {
                if (auto named = referenced_overlay(path, reference))
                    references_.insert(lower(*named));
            }
        }
        overlays_.emplace(lower(path), path);
        return true;
    }

    std::vector<std::string> AssetIndex::missing_references() const
    {
        std::vector<std::string> out;
        for (const std::string& reference : references_)
        {
            if (!overlays_.contains(reference))
                out.push_back(reference);
        }
        return out;
    }

    void AssetIndex::add(std::string_view symbol, std::string_view path)
    {
        // Kept sorted, so the nearest of several equally near files is always the same one.
        auto& files = symbols_[std::string(symbol)];
        const auto at = std::ranges::lower_bound(files, path);
        if (at == files.end() || *at != path)
            files.emplace(at, path);
        overlays_.emplace(lower(path), std::string(path));
    }

    std::optional<std::string> AssetIndex::find(std::string_view symbol, std::string_view near) const
    {
        const auto it = symbols_.find(symbol);
        if (it == symbols_.end() || it->second.empty())
            return std::nullopt;
        const std::string from = forward_slashes(near);
        const auto best = std::ranges::max_element(it->second, std::less<> { }, [&](const std::string& file) { return shared_start(file, from); });
        return *best;
    }

    std::optional<std::string> AssetIndex::overlay(std::string_view hint) const
    {
        const auto it = overlays_.find(lower(forward_slashes(hint)));
        if (it == overlays_.end())
            return std::nullopt;
        return it->second;
    }

    std::filesystem::path AssetIndex::file(std::string_view path) const
    {
        return root_ / (std::string(path) + std::string(kCommonEnding));
    }

} // namespace rct3::park
