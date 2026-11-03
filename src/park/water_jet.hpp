// SPDX-License-Identifier: GPL-3.0-or-later
//
// A jet of water shot from a nozzle, as water cannons and jumping fountains shoot it. Drops leave the nozzle
// at a steady rate, each with the speed the nozzle gives it then, and fall freely; the jet is the tube through
// them, so it bends into an arc, and swinging the nozzle bends it like a hose. Once stopped, the drops already
// out fly on and the jet is gone when the last has lived its time.
#pragma once

#include "math/glm.hpp"
#include "park/object_mesh.hpp"

#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace rct3::park
{

    inline constexpr float kJetGravity = 9.81f; ///< Metres a second squared

    struct JetDrop
    {
        glm::vec3 position { 0.0f };
        glm::vec3 velocity { 0.0f };
        float age = 0; ///< Seconds since it left the nozzle
    };

    class WaterJet
    {
    public:
        /// Each drop lives `life` seconds; `radius` is the tube's at the nozzle, and it widens by `spread` a second.
        WaterJet(float life, float radius, float spread);

        /// Where the nozzle is now and the speed it gives a drop. The first aim starts the jet at the nozzle.
        void aim(glm::vec3 nozzle, glm::vec3 velocity);
        /// No more drops leave.
        void stop();
        void advance(float seconds);

        [[nodiscard]] bool shooting() const noexcept
        {
            return shooting_;
        }
        [[nodiscard]] bool finished() const noexcept
        {
            return !shooting_ && drops_.empty();
        }
        /// Newest first, the one at the nozzle while it shoots.
        [[nodiscard]] std::span<const JetDrop> drops() const noexcept
        {
            return drops_;
        }
        [[nodiscard]] float radius(const JetDrop& drop) const noexcept
        {
            return radius_ + spread_ * drop.age;
        }
        /// Where the stream first goes below `ground` (the height at an x, z), between two drops; none if it
        /// stays above it.
        [[nodiscard]] std::optional<glm::vec3> landing(const std::function<float(glm::vec2)>& ground) const;

    private:
        float life_;
        float radius_;
        float spread_;
        bool shooting_ = false;
        glm::vec3 nozzle_ { 0.0f };
        glm::vec3 velocity_ { 0.0f };
        float pending_ = 0;
        std::vector<JetDrop> drops_;
    };

    struct Throw
    {
        glm::vec3 velocity { 0.0f };
        float seconds = 0; ///< Of flight
    };
    /// How to throw water from `from`, `angle` radians above the level, to land on `to`; none when `to` is too
    /// high to reach at that angle, or straight above.
    [[nodiscard]] std::optional<Throw> throw_to(glm::vec3 from, glm::vec3 to, float angle);

    /// The picture jets are drawn with: the waterfalls' sheet, scrolled along the jet.
    inline constexpr std::string_view kJetPicture = "rct3pp:waterfall-sheet";

    /// Adds the tubes of `jets` to `mesh` as one batch of kJetPicture, seen from `eye` (the rims, seen edge on,
    /// are whiter and less see-through), the picture running along each jet for `seconds`.
    void jet_mesh(std::span<const WaterJet* const> jets, glm::vec3 eye, float seconds, ObjectMesh& mesh);

} // namespace rct3::park
