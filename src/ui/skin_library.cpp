// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/skin_library.hpp"

#include <span>

namespace rct3::ui
{

    std::array<float, 2> SkinRef::size() const
    {
        if (layout.x_count == 0 || layout.y_count == 0)
            return { 0, 0 };
        return { layout.xs[layout.x_count - 1] - layout.xs[0], layout.ys[layout.y_count - 1] - layout.ys[0] };
    }

    std::string_view state_suffix(SkinState state)
    {
        switch (state)
        {
            case SkinState::Up:
                return "Up";
            case SkinState::UpOver:
                return "UpOver";
            case SkinState::Pressed:
                return "Pressed";
            case SkinState::Down:
                return "Down";
            case SkinState::DownOver:
                return "DownOver";
            case SkinState::Disabled:
                return "Disabled";
        }
        return { };
    }

    void SkinLibrary::add(std::string name, const SkinRef& skin)
    {
        skins_.insert_or_assign(std::move(name), skin);
    }

    const SkinRef* SkinLibrary::find(std::string_view name) const
    {
        const auto it = skins_.find(name);
        return it == skins_.end() ? nullptr : &it->second;
    }

    const SkinRef* SkinLibrary::find(std::string_view base, SkinState state) const
    {
        // The states to try, nearest first. A pressed toggle that is off looks like one held down.
        using enum SkinState;
        std::span<const SkinState> order;
        static constexpr std::array kUp = { Up };
        static constexpr std::array kUpOver = { UpOver, Up };
        static constexpr std::array kPressed = { Pressed, Down, Up };
        static constexpr std::array kDown = { Down, Pressed, Up };
        static constexpr std::array kDownOver = { DownOver, Down, Pressed, Up };
        static constexpr std::array kDisabled = { Disabled, Up };
        switch (state)
        {
            case Up:
                order = kUp;
                break;
            case UpOver:
                order = kUpOver;
                break;
            case Pressed:
                order = kPressed;
                break;
            case Down:
                order = kDown;
                break;
            case DownOver:
                order = kDownOver;
                break;
            case Disabled:
                order = kDisabled;
                break;
        }
        std::string name(base);
        for (const SkinState s : order)
        {
            name.resize(base.size());
            name += state_suffix(s);
            if (const SkinRef* skin = find(name))
                return skin;
        }
        return find(base);
    }

    void draw_skin(DrawList& list, const SkinRef* skin, const Rect& rect, Color tint)
    {
        if (skin != nullptr)
            draw_skin(list, skin->layout, rect, skin->texture, skin->texture_width, skin->texture_height, tint);
    }

    void draw_skin_state(DrawList& list, const SkinLibrary& skins, std::string_view family, SkinState state, const Rect& rect)
    {
        const SkinRef* face = skins.find(family, state);
        Color tint = kWhite;
        const bool fell_back = face == skins.find(family, SkinState::Up);
        if (fell_back && (state == SkinState::Pressed || state == SkinState::Down || state == SkinState::DownOver))
            tint = kPressedTint;
        else if (fell_back && state == SkinState::Disabled)
            tint = kDisabledTint;
        draw_skin(list, face, rect, tint);
    }

} // namespace rct3::ui
