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

#include "strings/ascii.hpp"
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

        /// Whatever the case of `name`: the game finds its pictures that way, and its text names some of
        /// them in another case than their own.
        [[nodiscard]] const SkinRef* find(std::string_view name) const;

        /// The family member for `state`, or the nearest one that exists, or the skin named `base`.
        [[nodiscard]] const SkinRef* find(std::string_view base, SkinState state) const;

        [[nodiscard]] std::size_t size() const
        {
            return skins_.size();
        }

    private:
        std::map<std::string, SkinRef, strings::LessIgnoringCase> skins_;
    };

    /// Draws `skin` stretched over `rect`. Nothing when `skin` is null, so a missing skin leaves a gap
    /// instead of failing.
    void draw_skin(DrawList& list, const SkinRef* skin, const Rect& rect, Color tint = kWhite);

    /// What draw_skin_state tints a resting face with when a family lacks the member for the state.
    inline constexpr Color kPressedTint = hex("#bebebe");
    inline constexpr Color kDisabledTint = hex("#808080a0");

    /// Draws the member of `family` for `state` over `rect`. When the family has no member of its own for a
    /// held, on or disabled state, its resting look is tinted darker or faded instead, so the state still
    /// shows on plain skins.
    void draw_skin_state(DrawList& list, const SkinLibrary& skins, std::string_view family, SkinState state, const Rect& rect);

} // namespace rct3::ui
