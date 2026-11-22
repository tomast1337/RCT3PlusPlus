// SPDX-License-Identifier: GPL-3.0-or-later
//
// The effects a display's shots fire besides fireworks: lasers and water jets. Each is a description with a few
// settings and timelines, kept the same way in three places: in its own file in the install or the player's
// folders (a laser file, a water jet file), and in a park beside each laser or water jet shot of its displays. A
// laser may draw pictures; a laser drawing file holds one picture alone. All are save containers
// (save/save_file.hpp). docs/show-effects.md describes the layouts.
//
// A timeline is a few control points, each a time in seconds and a value, held in no particular order; here it
// becomes a curve over the effect's time, 0 at its start and 1 at its end.
#pragma once

#include "core/types.hpp"
#include "math/glm.hpp"
#include "particles/effect.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rct3::save
{
    struct SaveFile;
    class ObjectIndex;
} // namespace rct3::save

namespace rct3::particles
{

    /// A water jet: how its stream changes over its time. Its curves run over that time, 0 at the start to 1 at
    /// the end.
    struct JetPattern
    {
        std::string name { };
        f32 seconds = 0;
        Curve pressure { };              ///< The speed the water leaves at, meters a second
        Curve elevation { };             ///< Lean from straight up, radians, toward the launcher's front
        Curve widening { };              ///< What the stream's width is multiplied by each second it flies
        std::array<Curve, 4> colour { }; ///< Red, green, blue and opacity, 0 to 1
    };

    /// How a laser's beams look.
    enum class LaserLook : u8
    {
        Cone = 0,    ///< Beams spread round a cone
        Plane = 1,   ///< Beams fanned out flat
        Drawing = 2, ///< One beam tracing its drawings
    };

    struct LaserPoint
    {
        glm::vec2 at { 0.0f }; ///< On the drawing pad, which runs about 0 to 32 across and 0 to 24 down
        bool lit = false;      ///< The beam shines on the way here from the point before; off, it jumps
    };

    /// A picture a laser draws, traced point after point.
    struct LaserDrawing
    {
        f32 seconds = 0; ///< One trace of it
        std::vector<LaserPoint> points { };
    };

    struct Laser
    {
        std::string name { };
        f32 seconds = 0;
        LaserLook look = LaserLook::Cone;
        i32 beams = 1;
        f32 power = 0;                   ///< Read as how far the beams reach, meters
        f32 trail_seconds = 0;           ///< How long a moving beam leaves a glow behind it
        f32 drawing_scale = 0;           ///< How large its drawings are thrown
        std::array<Curve, 4> colour { }; ///< Red, green, blue and opacity, 0 to 1
        Curve elevation { };             ///< Radians up from level
        Curve spin { };                  ///< Turns round its base, radians a second
        Curve width { };                 ///< How wide each beam spreads
        std::vector<LaserDrawing> drawings { };
    };

    enum class ShowEffectError : u8
    {
        Container, ///< Not a readable save container
        NoEffect,  ///< It holds no description of the kind asked for
        BadObject, ///< An object of it could not be decoded
    };

    [[nodiscard]] const char* to_string(ShowEffectError e) noexcept;

    /// The water jet a water jet file describes.
    [[nodiscard]] std::expected<JetPattern, ShowEffectError> read_water_jet(std::span<const std::byte> data);
    /// The laser a laser file describes, with its drawings.
    [[nodiscard]] std::expected<Laser, ShowEffectError> read_laser(std::span<const std::byte> data);
    /// The drawing in a laser drawing file.
    [[nodiscard]] std::expected<LaserDrawing, ShowEffectError> read_laser_drawing(std::span<const std::byte> data);

    /// The water jet object `id` of `file` describes, as a park keeps one for a shot; none when it is no water jet.
    [[nodiscard]] std::optional<JetPattern> water_jet_in(const save::SaveFile& file, const save::ObjectIndex& index, u64 id);
    /// The laser object `id` of `file` describes; none when it is no laser.
    [[nodiscard]] std::optional<Laser> laser_in(const save::SaveFile& file, const save::ObjectIndex& index, u64 id);

} // namespace rct3::particles
