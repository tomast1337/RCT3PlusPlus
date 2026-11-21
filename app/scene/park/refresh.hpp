// SPDX-License-Identifier: GPL-3.0-or-later
//
// The people and objects of the park scene (scene/park/scene.hpp), built again beside the frame as the view
// and the park change, and the drawing helpers the scene's other files share with them.
#pragma once

#include "core/types.hpp"
#include "scene/park/scene.hpp"

#include <chrono>
#include <future>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace rct3::scene::park
{

    /// Whether `build` has finished, without waiting for it.
    template<typename T> bool ready(const std::future<T>& build)
    {
        return build.valid() && build.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    /// Where `shown` stands now, facing +Z with +Y up: in their seat while riding, else where the simulation
    /// has walked them while that runs, else where they were saved. Reads the simulation, so only on the
    /// frame's thread.
    [[nodiscard]] glm::mat4 person_place(const ParkScene& scene, const ShownPerson& shown);
    /// Where person_place has `shown`.
    glm::vec3 standing_at(const ParkScene& scene, const ShownPerson& shown);

    /// Whether something standing at `at` may be in the view `transform` gives, with room to spare so it does
    /// not pop in at the edges while the view turns; `reach` is about its size, in clip units.
    [[nodiscard]] bool maybe_in_view(const glm::mat4& transform, const glm::vec3& at, f32 reach = 2.0f);

    /// The eyes of car `car`'s camera of kind `eye`, in its own space, when it has one; the nose camera only
    /// on the first car of its train.
    [[nodiscard]] std::optional<glm::mat4> ride_eye(const ParkScene& scene, usize car, rct3::park::object::RideEye eye);

    /// Where rider seat or fixed camera (`eye`) `at` of flat ride `ride` is in the park
    /// now, as it is drawn; nothing when the ride has no such seat or camera.
    [[nodiscard]] std::optional<glm::mat4> flat_view(const ParkScene& scene, save::model::RideIndex ride, FlatEye eye, usize at);

    /// Moves the ride cars to where the simulation has their trains, or back to where the park kept them
    /// while it does not run; and the scenery moving on its own near `eye` in `view`. Riding along, it also
    /// sets the rider's eyes, `elapsed` on from the last frame's. Cars passing a photo section have their riders
    /// photographed, as `options` allow, the photos named after the ride in `strings`.
    void place_cars(ParkScene& scene, const glm::vec3& eye, const glm::mat4& view, units::Seconds elapsed, const config::game::GameOptions& options, const ui::StringTable& strings);

    /// The people a photo taken from `eye` in `view` shows, each at the detail the photo's own distance asks:
    /// those the park draws near the player's view may be far from it.
    [[nodiscard]] rct3::park::object::ObjectMesh photo_people(const ParkScene& scene, const glm::vec3& eye, const glm::mat4& view);

    /// Builds the people meshes again as often as they need, all beside the frame: the moving ones as soon as
    /// the last are done, the riders a few times a second, the still ones when a person's level of detail
    /// changes. The still ones' levels are only taken on with them, so the moving ones keep to the same levels.
    void refresh_people(ParkScene& scene, const glm::vec3& eye, const glm::mat4& view, units::Seconds elapsed);

    /// The fades of the park's flowing water (park/water/flow.hpp), pool slides told by their kind's rules.
    [[nodiscard]] rct3::park::object::FlowFades flow_fades(const ParkScene& scene);

    /// The objects' meshes at the levels seen from `eye` for the chunks whose levels differ from `shown` (all
    /// when `shown_chunks` is not the park's count), or nothing when none do; without the supports unless
    /// `supports`. Reads only what is set when the park loads besides its arguments, so it may run beside the
    /// frame.
    std::optional<ObjectBuild> build_objects(const ParkScene& scene, const glm::vec3& eye, const rct3::park::object::LevelPlan& shown, usize shown_chunks, f32 band, const rct3::park::object::LevelScales& scales, bool supports, const rct3::park::object::Broken& broken = { }, const rct3::park::object::Chains& chains = { }, const rct3::park::object::FlowFades& fades = { }, const rct3::park::object::Broken& damaged = { });

    /// `vertices` with the bands of `bands` into `out`, for the crossing shaders.
    void crossing_vertices(std::vector<CrossingVertex>& out, std::span<const rct3::park::GroundVertex> vertices, std::span<const std::pair<glm::vec4, glm::vec2>> bands);

    /// Puts the chunks of ParkScene::objects_pending in their buffers until `budget` is spent, or all of them
    /// without one; the levels shown are the build's once all are in.
    void show_pending(render::Device& device, ParkScene& scene, std::optional<units::Seconds> budget);

    /// Puts `built` in the object buffers at once.
    void show_objects(render::Device& device, ParkScene& scene, ObjectBuild&& built);

    /// Takes on the objects' meshes once built, and looks at their levels again every
    /// kObjectRebuildInterval, building the meshes again beside the frame when one differs.
    void refresh_objects(render::Device& device, ParkScene& scene, const glm::vec3& eye, units::Seconds elapsed);

    /// Waits for the meshes being built beside the frame, dropping them.
    void finish_builds(ParkScene& scene);

    f32 scale_for(const render::Device& device, const ParkScene& scene);

    f32 aspect(const render::Device& device);

} // namespace rct3::scene::park
