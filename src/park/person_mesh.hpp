// SPDX-License-Identifier: GPL-3.0-or-later
//
// People posed and placed, as one mesh grouped by picture. Each piece is posed by blending the two keys
// of its motion around the person's time; a head carried by bones is posed by its bones' motion, each bone
// hanging from its parent, and its parts move with their bones.
//
// People's pictures are made of sixteen cells, four by four, each the same piece in another colour. A
// mesh's texture coordinates fall in the bottom left cell; the person's colour for the piece (0..15) picks
// the cell, counting along the bottom row first, then up. The head's skin always takes the skin colour.
//
// A person may hold a shape (an umbrella) in their left hand: it stands upright at the point the body's
// left-hand socket reaches in the person's motion, turned with the person.
#pragma once

#include "park/moving_meshes.hpp"
#include "park/object_mesh.hpp"
#include "park/person_models.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rct3::park
{

    /// What one person looks like.
    struct PersonLook
    {
        const PartSet* head = nullptr;
        const PartSet* body = nullptr;
        const PartSet* legs = nullptr;
        bool swimwear = false;                      ///< Draw the sets' swimwear pieces and head
        std::array<std::uint8_t, 3> colours { };    ///< Skin, top and bottom, each 0..15
        std::array<std::uint32_t, 3> optionals { }; ///< Bit k turns on optional piece k of head, body, legs
    };

    /// One person to draw.
    struct PlacedPerson
    {
        PersonLook look { };
        std::size_t level = 0;             ///< Of detail, 0 the finest
        std::int32_t motion = 0;           ///< By number (see person_motions.hpp)
        float time = 0;                    ///< Seconds into the motion
        glm::mat4 world { 1.0f };          ///< Model to park
        const ObjectModel* held = nullptr; ///< Held in the left hand, drawn at its finest level in its own picture
    };

    /// How far a person's texture coordinates move to reach cell `colour`.
    [[nodiscard]] glm::vec2 colour_cell(std::uint8_t colour);

    /// The middle of socket piece `socket` (see PieceModel::left_hand) in `person`'s motion, in their own space;
    /// nothing when the motion has no keys.
    [[nodiscard]] std::optional<glm::vec3> socket_point(const PieceModel& socket, const PlacedPerson& person);

    /// Where the left hand of `person` is, in their own space; nothing when their body has no socket for it.
    [[nodiscard]] std::optional<glm::vec3> left_hand(const PersonLibrary& library, const PlacedPerson& person);

    /// How long `person`'s motion lasts, played once through, seconds; 0 when their body has none.
    [[nodiscard]] float motion_seconds(const PersonLibrary& library, const PlacedPerson& person);

    /// How fast the walk of `legs` (a legs piece), or its other way of going on foot `motion` (a run, a tired
    /// walk), carries a walker along, in park units for each second of the motion: the speed at which a foot
    /// on the ground slides back beneath them. Played at this pace the feet stay put. Nothing when the piece
    /// has no such motion to measure.
    [[nodiscard]] std::optional<float> walk_pace(const PieceModel& legs, std::int32_t motion);

    /// The triangles of people gathered for one picture.
    struct PersonRun
    {
        std::vector<GroundVertex> vertices { };
        std::vector<std::uint32_t> indices { };
    };
    /// Those of every picture, by picture. Kept by a caller that builds mesh after mesh and passed to each, so
    /// the lists' memory is reused.
    using PersonRuns = std::map<std::string, PersonRun, std::less<>>;

    /// The mesh of `people`, gathered in `runs`, which is emptied first.
    [[nodiscard]] ObjectMesh person_mesh(const PersonLibrary& library, std::span<const PlacedPerson> people, PersonRuns& runs);

    /// Groups of people that move together, such as the riders of each car: each group one piece of the
    /// result, in the group's own space, and triangles of the same picture drawn together whatever their
    /// group. `runs` and `gathered` are emptied first; a caller building again and again keeps them, so their
    /// memory is reused.
    [[nodiscard]] MovingMeshes carried_people_meshes(const PersonLibrary& library, std::span<const std::span<const PlacedPerson>> groups, PersonRuns& runs, PersonRuns& gathered);

} // namespace rct3::park
