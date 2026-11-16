// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park's objects grouped into square chunks of the map for drawing, each with buffers of its own: a pass draws
// only the chunks its view reaches, and when objects change level only the chunks holding them are built again.
// An object belongs to the chunk of the tile it stands on, however far its shape reaches; a chunk's box is taken
// round its shapes, so a pass sees the whole of it.
#pragma once

#include "math/glm.hpp"
#include "park/object/mesh.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rct3::park::object
{

    /// How a map `width` by `height` tiles is cut into chunks `tiles` square, row by row from the first tile.
    struct ChunkGrid
    {
        int across = 0;
        int down = 0;
        int tiles = 1;

        [[nodiscard]] static ChunkGrid of(int width, int height, int tiles);
        [[nodiscard]] std::size_t count() const;
        /// The chunk holding the tile at `col`, `row`; tiles off the map go to the nearest chunk along its edge.
        [[nodiscard]] std::size_t at(int col, int row) const;
    };

    /// By chunk, the indices of `objects` standing in it, rising.
    [[nodiscard]] std::vector<std::vector<std::uint32_t>> chunk_members(std::span<const save::model::PlacedObject> objects, const ChunkGrid& grid);

    /// By chunk of `members`, whether any object in it is planned differently in `now` than in `shown`. When the
    /// two plans are for different numbers of objects every chunk has changed.
    [[nodiscard]] std::vector<bool> changed_chunks(const LevelPlan& now, const LevelPlan& shown, std::span<const std::vector<std::uint32_t>> members);

    /// The box round some shapes, empty until a point is taken in.
    struct ShapeBox
    {
        glm::vec3 low { 0.0f };
        glm::vec3 high { 0.0f };
        bool empty = true;

        void take(const glm::vec3& point);
        /// Round every vertex of `mesh`.
        void take(const ObjectMesh& mesh);
        /// Round each billboard of `boards` turned any way.
        void take(const Billboards& boards);
    };

} // namespace rct3::park::object
