// SPDX-License-Identifier: GPL-3.0-or-later
//
// A jet of water shot from a nozzle, as water cannons and jumping fountains shoot it. Drops leave the nozzle
// at a steady rate, each with the speed the nozzle gives it then, and fall freely; the jet is the tube through
// them, so it bends into an arc, and swinging the nozzle bends it like a hose. Once stopped, the drops already
// out fly on and the jet is gone when the last has lived its time. A jet may break up as it goes: whole and
// clear at the nozzle, wider, whiter, ragged and see-through toward the end of its life, fading away there.
#pragma once

#include "math/glm.hpp"
#include "math/texture_shift.hpp"
#include "park/object/mesh.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace rct3::park::water
{

    inline constexpr float kJetGravity = 9.81f; ///< Metres a second squared

    struct JetDrop
    {
        glm::vec3 position { 0.0f };
        glm::vec3 velocity { 0.0f };
        float age = 0;            ///< Seconds since it left the nozzle
        std::uint32_t serial = 0; ///< Which of the jet's drops it is, counting from its first, for a ragged look that holds as it flies
    };

    class WaterJet
    {
    public:
        /// Each drop lives `life` seconds; `radius` is the tube's at the nozzle, and it widens by `spread` a second.
        /// `breakup`, 0 to 1, is how much it breaks into spray toward the end of its life; 0 stays whole.
        WaterJet(float life, float radius, float spread, float breakup = 0);

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
        /// Wider as it spreads, and more as it breaks up.
        [[nodiscard]] float radius(const JetDrop& drop) const noexcept;
        /// How broken up the water is at `drop`, 0 whole to `breakup` at the end of its life.
        [[nodiscard]] float broken(const JetDrop& drop) const noexcept;
        /// The share of its life `drop` has lived, 0 to 1.
        [[nodiscard]] float aged(const JetDrop& drop) const noexcept;
        /// Where the stream first goes below `ground` (the height at an x, z), between two drops; none if it
        /// stays above it.
        [[nodiscard]] std::optional<glm::vec3> landing(const std::function<float(glm::vec2)>& ground) const;

    private:
        float life_;
        float radius_;
        float spread_;
        float breakup_;
        std::uint32_t shot_ = 0;
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

    /// The overlay holding the jets' pictures, the game's running water: seen in the game's install.
    inline constexpr std::string_view kJetFile = "Waterfall/Waterfall.common.ovl";

    /// One of the jets' pictures: its texture's name in the overlay and its key among the scene's pictures, and
    /// the same of the second picture drawn over it (render::Shader::LitLayerAdded), running at another speed so
    /// the water shimmers rather than slides.
    struct JetPicture
    {
        std::string_view texture { };
        std::string_view key { };
        std::string_view layer_texture { };
        std::string_view layer_key { };
        float layer_lag = 0; ///< Pictures a second the second picture runs behind the first
    };

    /// The clear water of a jet, and the froth where it breaks up, in the order jet_mesh's batches come in.
    inline constexpr std::array<JetPicture, 2> kJetPictures { {
        { "Waterfall2", "rct3pp:jet", "Waterfall2B", "rct3pp:jet-b", 0.3f },
        { "WaterfallFloorFroth", "rct3pp:froth", "WaterfallFloorFrothB", "rct3pp:froth-b", -0.4f },
    } };
    /// The picture jets are drawn with, scrolled along the jet.
    inline constexpr std::string_view kJetPicture = kJetPictures[0].key;
    /// And where it breaks up, the froth over it.
    inline constexpr std::string_view kJetSpray = kJetPictures[1].key;

    /// How far the first picture of `picture` is moved along v past its second after `seconds`: the mesh's
    /// coordinates are where the second is read, and the draw moves the first on by this.
    [[nodiscard]] math::TextureShift jet_layer_scroll(const JetPicture& picture, float seconds);

    /// Adds the tubes of `jets` to `mesh` as one batch of kJetPicture, and one of kJetSpray where they break up, seen from `eye` (the rims, seen edge on,
    /// are less see-through; water breaking up is whiter, ragged and clearer), the picture running along each jet
    /// for `seconds`. A jet too thin to see from afar is drawn wider and fainter.
    void jet_mesh(std::span<const WaterJet* const> jets, glm::vec3 eye, float seconds, object::ObjectMesh& mesh);

} // namespace rct3::park::water
