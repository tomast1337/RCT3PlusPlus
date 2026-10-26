// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/person_mesh.hpp"

#include "park/flexi_colours.hpp"
#include "park/person_motions.hpp"

#include <algorithm>
#include <cstring>
#include <map>

namespace rct3::park
{

    namespace
    {

        constexpr float kCell = 0.25f;

        using Run = PersonRun;
        using Runs = PersonRuns;

        std::uint32_t word_at(const std::vector<std::byte>& bytes, std::size_t at)
        {
            std::uint32_t v = 0;
            std::memcpy(&v, bytes.data() + at, sizeof v);
            return v;
        }

        glm::vec3 point_at(const ovl::FrameAnimation& motion, std::size_t key_start, std::uint16_t point)
        {
            const std::uint32_t word = word_at(motion.keys, key_start + std::size_t { point } * 4);
            return { motion.offset[0] + static_cast<float>(word >> 21) * motion.scale[0], motion.offset[1] + static_cast<float>((word >> 11) & 0x3FF) * motion.scale[1], motion.offset[2] + static_cast<float>(word & 0x7FF) * motion.scale[2] };
        }

        glm::vec3 normal_at(const ovl::FrameAnimation& motion, std::size_t at)
        {
            glm::vec3 n { 0.0f };
            for (glm::length_t c = 0; c < 3; ++c)
                n[c] = (static_cast<float>(std::to_integer<int>(motion.keys[at + static_cast<std::size_t>(c)])) - 127.5f) / 127.5f;
            return n;
        }

        void add_piece(Runs& runs, const PieceModel& piece, const PlacedPerson& person, std::uint8_t colour)
        {
            const ovl::FrameMesh& mesh = piece.mesh;
            if (mesh.animations.empty() || mesh.corners.empty())
                return;
            const std::size_t index = piece.motions[person.motion];
            const ovl::FrameAnimation& motion = mesh.animations[index < mesh.animations.size() ? index : 0];
            if (motion.times.empty())
                return;
            const KeyBlend blend = blend_keys(motion.times, person.time, motion_repeats(person.motion));
            const std::size_t size = mesh.key_size();
            const std::size_t from = blend.from * size;
            const std::size_t to = blend.to * size;
            const std::size_t normals = std::size_t { mesh.points } * 4;
            if (std::max(from, to) + size > motion.keys.size())
                return;

            Run& run = runs[piece.texture];
            const auto base = static_cast<std::uint32_t>(run.vertices.size());
            const glm::mat3 turn { person.world };
            const glm::vec2 cell = colour_cell(colour);
            for (const ovl::FrameCorner& corner : mesh.corners)
            {
                if (corner.point >= mesh.points || corner.normal >= mesh.normals)
                {
                    run.vertices.push_back({ glm::vec3(person.world[3]), glm::vec4(1.0f), { }, { 0.0f, 1.0f, 0.0f } });
                    continue;
                }
                const glm::vec3 at = glm::mix(point_at(motion, from, corner.point), point_at(motion, to, corner.point), blend.along);
                const std::size_t n = normals + std::size_t { corner.normal } * 3;
                const glm::vec3 normal = glm::mix(normal_at(motion, from + n), normal_at(motion, to + n), blend.along);
                run.vertices.push_back({ glm::vec3(person.world * glm::vec4(at, 1.0f)), glm::vec4(1.0f), glm::vec2(corner.uv[0], corner.uv[1]) + cell, turn * normal });
            }
            for (const std::uint16_t i : mesh.indices)
                run.indices.push_back(base + (i < mesh.corners.size() ? i : 0));
        }

        /// The bones of `head` posed for `person`, each as model to park.
        std::vector<glm::mat4> pose_bones(const HeadModel& head, const PlacedPerson& person)
        {
            std::vector<glm::mat4> bones(head.parents.size(), person.world);
            const std::size_t index = head.motion_index[person.motion];
            const ovl::SimpleAnim* motion = nullptr;
            if (index < head.motions.size() && head.motions[index])
                motion = &*head.motions[index];
            else if (!head.motions.empty() && head.motions.front())
                motion = &*head.motions.front();
            if (motion == nullptr || motion->times.empty() || motion->channels == 0)
                return bones;
            const KeyBlend blend = blend_keys(motion->times, person.time, motion_repeats(person.motion));
            const auto sample = [&](std::size_t key, std::size_t bone) -> const ovl::SimpleAnimSample& { return motion->samples[key * motion->channels + bone]; };
            for (std::size_t bone = 0; bone < bones.size() && bone < motion->channels; ++bone)
            {
                if ((std::max(blend.from, blend.to) + 1) * motion->channels > motion->samples.size())
                    break;
                const ovl::SimpleAnimSample& a = sample(blend.from, bone);
                const ovl::SimpleAnimSample& b = sample(blend.to, bone);
                // The two stored axes are blended as they are; they stay close enough to square between keys.
                std::array<glm::vec3, 3> rows { };
                for (std::size_t r = 0; r < 2; ++r)
                {
                    for (glm::length_t c = 0; c < 3; ++c)
                    {
                        const auto unpack = [&](const ovl::SimpleAnimSample& s) { return static_cast<float>(s.packed[r * 3 + static_cast<std::size_t>(c)]) / 32767.0f; };
                        rows[r][c] = glm::mix(unpack(a), unpack(b), blend.along);
                    }
                }
                rows[2] = glm::cross(rows[0], rows[1]);
                const glm::vec3 at = glm::mix(glm::vec3(a.vector[0], a.vector[1], a.vector[2]), glm::vec3(b.vector[0], b.vector[1], b.vector[2]), blend.along);
                const glm::mat4 local { glm::vec4(rows[0], 0.0f), glm::vec4(rows[1], 0.0f), glm::vec4(rows[2], 0.0f), glm::vec4(at, 1.0f) };
                const std::int32_t parent = head.parents[bone];
                bones[bone] = (parent >= 0 ? bones[static_cast<std::size_t>(parent)] : person.world) * local;
            }
            return bones;
        }

        void add_head(Runs& runs, const HeadModel& head, const PlacedPerson& person, std::size_t level)
        {
            if (level >= head.skins.size() || !head.skins[level])
                return;
            const ovl::SimpleSkin& skin = *head.skins[level];
            const std::vector<glm::mat4> bones = pose_bones(head, person);
            Run& run = runs[head.texture];
            const auto base = static_cast<std::uint32_t>(run.vertices.size());
            const glm::vec2 cell = colour_cell(person.look.colours[0]);
            std::size_t vertex = 0;
            for (std::size_t part = 0; part < skin.part_vertex_counts.size(); ++part)
            {
                const glm::mat4& bone = part < bones.size() ? bones[part] : person.world;
                const glm::mat3 turn { bone };
                for (std::uint32_t k = 0; k < skin.part_vertex_counts[part] && vertex < skin.vertices.size(); ++k, ++vertex)
                {
                    const ovl::SimpleSkinVertex& v = skin.vertices[vertex];
                    const glm::vec3 at { v.position[0], v.position[1], v.position[2] };
                    const glm::vec3 normal { v.normal[0], v.normal[1], v.normal[2] };
                    run.vertices.push_back({ glm::vec3(bone * glm::vec4(at, 1.0f)), glm::vec4(1.0f), glm::vec2(v.uv[0], v.uv[1]) + cell, glm::normalize(turn * normal) });
                }
            }
            const auto count = static_cast<std::uint32_t>(run.vertices.size()) - base;
            for (const std::uint16_t i : skin.indices)
                run.indices.push_back(base + (i < count ? i : 0));
        }

        void add_set(Runs& runs, const PersonLibrary& library, const PartSet* set, const PlacedPerson& person, std::size_t section)
        {
            if (set == nullptr)
                return;
            const std::size_t level = std::min(person.level, kPersonLevels - 1);
            const PartLevel& pieces = (person.look.swimwear ? set->swim_levels : set->levels)[level];
            for (const std::size_t piece : pieces.fixed)
            {
                const PieceModel& model = library.piece(piece);
                if (model.left_hand)
                    continue;
                add_piece(runs, model, person, person.look.colours[model.channel < 3 ? model.channel : 0]);
            }
            for (std::size_t k = 0; k < pieces.optional.size() && k < 32; ++k)
            {
                if ((person.look.optionals[section] >> k & 1u) == 0 || pieces.optional[k] == kNoPiece)
                    continue;
                const PieceModel& model = library.piece(pieces.optional[k]);
                add_piece(runs, model, person, person.look.colours[model.channel < 3 ? model.channel : 0]);
            }
            const auto& head = person.look.swimwear ? set->swim_head : set->head;
            if (head && level < 2)
                add_head(runs, library.head(*head), person, level);
        }

        void add_held(Runs& runs, const ObjectModel& held, const glm::mat4& place)
        {
            if (held.levels.empty())
                return;
            const glm::mat3 turn { place };
            for (const ModelPart& part : held.levels.front().parts)
            {
                Run& run = runs[painted_name(part.texture, part.slots, { })];
                const auto base = static_cast<std::uint32_t>(run.vertices.size());
                for (const ModelVertex& v : part.vertices)
                    run.vertices.push_back({ glm::vec3(place * glm::vec4(v.position, 1.0f)), glm::vec4(1.0f), v.uv, glm::normalize(turn * v.normal) });
                for (const std::uint32_t i : part.indices)
                    run.indices.push_back(base + (i < part.vertices.size() ? i : 0));
            }
        }

    } // namespace

    std::optional<glm::vec3> left_hand(const PersonLibrary& library, const PlacedPerson& person)
    {
        const PartSet* body = person.look.body;
        if (body == nullptr)
            return std::nullopt;
        const std::size_t level = std::min(person.level, kPersonLevels - 1);
        for (const std::size_t piece : (person.look.swimwear ? body->swim_levels : body->levels)[level].fixed)
        {
            if (const PieceModel& model = library.piece(piece); model.left_hand)
                return socket_point(model, person);
        }
        return std::nullopt;
    }

    std::optional<glm::vec3> socket_point(const PieceModel& socket, const PlacedPerson& person)
    {
        // The socket's triangle, posed as a piece would be, at the person's own place.
        PlacedPerson own = person;
        own.world = glm::mat4(1.0f);
        Runs runs;
        add_piece(runs, socket, own, 0);
        const auto& posed = runs[socket.texture].vertices;
        if (posed.empty())
            return std::nullopt;
        glm::vec3 sum { 0.0f };
        for (const GroundVertex& v : posed)
            sum += v.position;
        return sum / static_cast<float>(posed.size());
    }

    std::optional<float> walk_pace(const PieceModel& legs, std::int32_t number)
    {
        // Over each step from key to key, the lowest point is a foot on the ground; its slide back (people face
        // +z) over the step's time is the walker's speed. The middle value leaves out the steps where the feet
        // swap.
        const ovl::FrameMesh& mesh = legs.mesh;
        const std::size_t walk = legs.motions[number];
        if (mesh.points == 0 || walk >= mesh.animations.size())
            return std::nullopt;
        const ovl::FrameAnimation& motion = mesh.animations[walk];
        const std::size_t keys = motion.times.size();
        const std::size_t size = mesh.key_size();
        if (keys < 2 || keys * size > motion.keys.size())
            return std::nullopt;
        std::vector<float> speeds;
        for (std::size_t k = 0; k < keys; ++k)
        {
            std::uint16_t lowest = 0;
            for (std::uint16_t p = 1; p < mesh.points; ++p)
                if (point_at(motion, k * size, p).y < point_at(motion, k * size, lowest).y)
                    lowest = p;
            const float step = motion.times[k] - (k == 0 ? 0.0f : motion.times[k - 1]);
            if (step > 0)
                speeds.push_back((point_at(motion, k * size, lowest).z - point_at(motion, (k + 1) % keys * size, lowest).z) / step);
        }
        if (speeds.empty())
            return std::nullopt;
        std::ranges::nth_element(speeds, speeds.begin() + static_cast<std::ptrdiff_t>(speeds.size() / 2));
        const float pace = speeds[speeds.size() / 2];
        return pace > 0 ? std::optional(pace) : std::nullopt;
    }

    glm::vec2 colour_cell(std::uint8_t colour)
    {
        return { static_cast<float>(colour & 3) * kCell, -static_cast<float>((colour >> 2) & 3) * kCell };
    }

    namespace
    {
        /// Empties `runs`, then fills them with `people`.
        void gather_people(Runs& runs, const PersonLibrary& library, std::span<const PlacedPerson> people)
        {
            for (auto& [texture, run] : runs)
            {
                run.vertices.clear();
                run.indices.clear();
            }
            for (const PlacedPerson& person : people)
            {
                add_set(runs, library, person.look.head, person, 0);
                add_set(runs, library, person.look.body, person, 1);
                add_set(runs, library, person.look.legs, person, 2);
                if (person.held != nullptr)
                    if (const auto hand = left_hand(library, person))
                        add_held(runs, *person.held, glm::translate(person.world, *hand));
            }
        }
    } // namespace

    ObjectMesh person_mesh(const PersonLibrary& library, std::span<const PlacedPerson> people, PersonRuns& runs)
    {
        gather_people(runs, library, people);
        ObjectMesh mesh { .placed = people.size() };
        for (auto& [texture, run] : runs)
        {
            if (run.vertices.empty())
                continue; // A picture only an earlier mesh had
            const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
            mesh.batches.push_back({ texture, texture, { }, PartBlend::Opaque, static_cast<std::uint32_t>(mesh.indices.size()), static_cast<std::uint32_t>(run.indices.size()) });
            mesh.vertices.insert(mesh.vertices.end(), run.vertices.begin(), run.vertices.end());
            for (const std::uint32_t i : run.indices)
                mesh.indices.push_back(base + i);
        }
        return mesh;
    }

    MovingMeshes carried_people_meshes(const PersonLibrary& library, std::span<const std::span<const PlacedPerson>> groups, PersonRuns& runs, PersonRuns& gathered)
    {
        for (auto& [texture, run] : gathered)
            run.indices.clear();
        MovingMeshes out;
        for (const std::span<const PlacedPerson> people : groups)
        {
            // The group's vertices stay together, as one piece to move; its triangles join those of the same
            // picture from every group.
            gather_people(runs, library, people);
            for (const auto& [texture, run] : runs)
            {
                const auto base = static_cast<std::uint32_t>(out.own.size());
                out.own.insert(out.own.end(), run.vertices.begin(), run.vertices.end());
                auto& indices = gathered[texture].indices;
                for (const std::uint32_t i : run.indices)
                    indices.push_back(base + i);
            }
            out.ends.push_back(static_cast<std::uint32_t>(out.own.size()));
            out.mesh.placed += people.size();
        }
        for (const auto& [texture, run] : gathered)
        {
            if (run.indices.empty())
                continue;
            out.mesh.batches.push_back({ texture, texture, { }, PartBlend::Opaque, static_cast<std::uint32_t>(out.mesh.indices.size()), static_cast<std::uint32_t>(run.indices.size()) });
            out.mesh.indices.insert(out.mesh.indices.end(), run.indices.begin(), run.indices.end());
        }
        out.mesh.vertices = out.own;
        out.places.assign(groups.size(), glm::mat4(1.0f));
        return out;
    }

} // namespace rct3::park
