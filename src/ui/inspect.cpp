// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/inspect.hpp"

#include "core/maybe.hpp"

#include <cstdlib>
#include <format>
#include <memory>
#include <typeinfo>

#if __has_include(<cxxabi.h>)
    #include <cxxabi.h>
#endif

namespace rct3::ui
{
    bool in_tree(const Widget& root, const Widget* target)
    {
        if (&root == target)
            return true;
        for (const auto& c : root.children())
        {
            if (in_tree(*c, target))
                return true;
        }
        return false;
    }

    namespace
    {
        const Widget* shown_at(const Widget& w, Point p)
        {
            if (!w.visible || !contains(w.rect(), p))
                return nullptr;
            for (auto it = w.children().rbegin(); it != w.children().rend(); ++it)
            {
                if (const Widget* found = shown_at(**it, p))
                    return found;
            }
            const bool shows = w.takes_pointer() ? w.covers(p) : w.children().empty();
            return shows ? &w : nullptr;
        }
    } // namespace

    std::optional<const Widget&> widget_at(const Widget& root, Point p)
    {
        for (auto it = root.children().rbegin(); it != root.children().rend(); ++it)
        {
            if (const Widget* found = shown_at(**it, p))
                return *found;
        }
        return std::nullopt;
    }

    std::string type_name(const Widget& w)
    {
        std::string name = typeid(w).name();
#if __has_include(<cxxabi.h>)
        int status = 0;
        const std::unique_ptr<char, void (*)(void*)> plain(abi::__cxa_demangle(name.c_str(), nullptr, nullptr, &status), std::free);
        if (status == 0)
            name = plain.get();
#endif
        const std::size_t colons = name.rfind("::");
        return colons == std::string::npos ? name : name.substr(colons + 2);
    }

    std::string widget_path(const Widget& w)
    {
        std::string path;
        for (const Widget* at = &w; at->parent(); at = at->parent())
            path = path.empty() ? std::string(at->name()) : std::format("{}/{}", at->name(), path);
        return path;
    }

} // namespace rct3::ui
