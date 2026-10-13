// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widget.hpp"

#include <algorithm>

namespace rct3::ui
{

    Widget::Widget(std::string name)
        : name_(std::move(name))
    {
    }

    void Widget::adopt(std::unique_ptr<Widget> child)
    {
        child->parent_ = this;
        children_.push_back(std::move(child));
    }

    std::unique_ptr<Widget> Widget::remove(const Widget& child)
    {
        const auto it = std::ranges::find_if(children_, [&](const auto& c) { return c.get() == &child; });
        if (it == children_.end())
            return nullptr;
        std::unique_ptr<Widget> detached = std::move(*it);
        children_.erase(it);
        detached->parent_ = nullptr;
        return detached;
    }

    void Widget::raise(const Widget& child)
    {
        const auto it = std::ranges::find_if(children_, [&](const auto& c) { return c.get() == &child; });
        if (it != children_.end())
            std::rotate(it, it + 1, children_.end());
    }

    void Widget::lower(const Widget& child)
    {
        const auto it = std::ranges::find_if(children_, [&](const auto& c) { return c.get() == &child; });
        if (it != children_.end())
            std::rotate(children_.begin(), it, it + 1);
    }

    Widget* Widget::find(std::string_view path) const
    {
        const std::size_t slash = path.find('/');
        const std::string_view head = path.substr(0, slash);
        for (const auto& c : children_)
        {
            if (c->name_ != head)
                continue;
            if (slash == std::string_view::npos)
                return c.get();
            if (Widget* found = c->find(path.substr(slash + 1)))
                return found;
        }
        return nullptr;
    }

} // namespace rct3::ui
