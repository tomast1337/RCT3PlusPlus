// SPDX-License-Identifier: GPL-3.0-or-later
//
// The skins widgets draw with, by name, each tied to the texture it cuts. Built by whoever uploads the
// textures; widgets only look names up.
//
// Interactive skins come in families that share a base name and differ by a state suffix: "Up" at rest,
// "UpOver" under the pointer, "Pressed" while held, "Down" and "DownOver" for a toggle that is on, and
// "Disabled". Families rarely have every member, so a lookup falls back to the nearest state that exists,
// and finally to the base name itself.
#pragma once

#include "ui/draw_list.hpp"
#include "ui/skin.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace rct3::ui
{

    struct SkinRef
    {
        SkinLayout layout;
        TextureId texture = 0;
        float texture_width = 0;
        float texture_height = 0;

        /// The size the skin has in its texture: the size it is drawn at when it is not stretched.
        [[nodiscard]] std::array<float, 2> size() const;
    };

    enum class SkinState : std::uint8_t
    {
        Up,
        UpOver,
        Pressed,
        Down,
        DownOver,
        Disabled,
    };

    [[nodiscard]] std::string_view state_suffix(SkinState state);

    class SkinLibrary
    {
    public:
        void add(std::string name, const SkinRef& skin);

        [[nodiscard]] const SkinRef* find(std::string_view name) const;

        /// The family member for `state`, or the nearest one that exists, or the skin named `base`.
        [[nodiscard]] const SkinRef* find(std::string_view base, SkinState state) const;

        [[nodiscard]] std::size_t size() const
        {
            return skins_.size();
        }

    private:
        std::map<std::string, SkinRef, std::less<>> skins_;
    };

    /// Draws `skin` stretched over `rect`. Nothing when `skin` is null, so a missing skin leaves a gap
    /// instead of failing.
    void draw_skin(DrawList& list, const SkinRef* skin, const Rect& rect, Color tint = kWhite);

} // namespace rct3::ui
