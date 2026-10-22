// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/inspect.hpp"

#include "core/maybe.hpp"

#include <algorithm>
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
        const Widget* shown_at(const Widget& w, Point p, const Rect& screen)
        {
            if (!w.visible || !contains(w.rect(), p))
                return nullptr;
            for (auto it = w.children().rbegin(); it != w.children().rend(); ++it)
            {
                if (const Widget* found = shown_at(**it, p, screen))
                    return found;
            }
            // A childless widget over the whole screen letting the pointer through is an overlay drawing here
            // and there, such as money floating up from the park; it would be found everywhere.
            const bool overlay = w.rect().x0 <= screen.x0 && w.rect().y0 <= screen.y0 && w.rect().x1 >= screen.x1 && w.rect().y1 >= screen.y1;
            const bool shows = w.takes_pointer() ? w.covers(p) : w.children().empty() && !overlay;
            return shows ? &w : nullptr;
        }
    } // namespace

    std::optional<const Widget&> widget_at(const Widget& root, Point p)
    {
        for (auto it = root.children().rbegin(); it != root.children().rend(); ++it)
        {
            if (const Widget* found = shown_at(**it, p, root.rect()))
                return *found;
        }
        return std::nullopt;
    }

    Rect covered_bounds(const Widget& w, float step)
    {
        const Rect r = w.rect();
        if (!w.takes_pointer())
            return r;
        Rect found { r.x1, r.y1, r.x0, r.y0 };
        for (float y = r.y0 + step / 2; y < r.y1; y += step)
            for (float x = r.x0 + step / 2; x < r.x1; x += step)
            {
                if (!w.covers({ x, y }))
                    continue;
                found = { std::min(found.x0, x - step / 2), std::min(found.y0, y - step / 2), std::max(found.x1, x + step / 2), std::max(found.y1, y + step / 2) };
            }
        if (found.x0 > found.x1)
            return { r.x0, r.y0, r.x0, r.y0 };
        return { std::max(found.x0, r.x0), std::max(found.y0, r.y0), std::min(found.x1, r.x1), std::min(found.y1, r.y1) };
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
