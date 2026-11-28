// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park/debug_tab.hpp"

#include "career/career_file.hpp"
#include "core/types.hpp"
#include "imgui.h"
#include "park/person/motions.hpp"
#include "scene/enclosure_tools.hpp"
#include "scene/land_tools.hpp"
#include "scene/objects.hpp"
#include "scene/park/fireworks.hpp"
#include "scene/park/scene.hpp"
#include "scene/park/waterfalls.hpp"
#include "simulation/actions/queue.hpp"
#include "simulation/day_clock.hpp"
#include "simulation/wild_birds.hpp"
#include "strings/unicode.hpp"
#include "ui/interface_assets.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace rct3::scene::park
{

    namespace
    {
        /// Ducks in a flock, and gulls, that the buttons add. Chosen by eye: a flock the size of a natural one, and
        /// a few gulls to see them cross.
        constexpr usize kDebugFlock = 12;
        constexpr usize kDebugGulls = 3;
        constexpr i64 kDebugFenceKnocks = 8;

        /// The developer action a tool is, for those that change the park.
        std::optional<simulation::actions::DeveloperTool> developer_tool(DebugTool tool)
        {
            using D = simulation::actions::DeveloperTool;
            switch (tool)
            {
                case DebugTool::AddMoney:
                    return D::AddMoney;
                case DebugTool::BuyAllLand:
                    return D::BuyAllLand;
                case DebugTool::FinishResearch:
                    return D::FinishResearch;
                case DebugTool::AddGuests:
                    return D::AddGuests;
                case DebugTool::RemoveGuests:
                    return D::RemoveGuests;
                case DebugTool::RefreshGuests:
                    return D::RefreshGuests;
                case DebugTool::BreakRides:
                    return D::BreakRides;
                case DebugTool::ResetRides:
                    return D::ResetRides;
                case DebugTool::RemoveAnimals:
                    return D::RemoveAnimals;
                case DebugTool::RefreshAnimals:
                    return D::RefreshAnimals;
                case DebugTool::DropLitter:
                    return D::DropLitter;
                case DebugTool::ClearLitter:
                    return D::ClearLitter;
                case DebugTool::ResetItemNames:
                    return D::ResetItemNames;
                case DebugTool::SlowGuests:
                    return D::SlowGuests;
                case DebugTool::PartiesHuddleOften:
                    return D::PartiesHuddleOften;
                case DebugTool::KnockFences:
                    return D::KnockFences;
                case DebugTool::BreedAnimals:
                    return D::BreedAnimals;
                case DebugTool::AgeAnimals:
                    return D::AgeAnimals;
                default:
                    return std::nullopt;
            }
        }

        /// Our text under `key`, as ImGui takes it.
        std::string label(const ui::InterfaceAssets& assets, std::string_view key)
        {
            return strings::to_utf8(assets.strings->get(key));
        }

        /// A tool picked in the tab: queued where it changes the park, else used at once.
        void pick_tool(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, DebugTool tool, i64 amount)
        {
            if (const auto action = developer_tool(tool))
                simulation::actions::ask(scene.actions, scene.sim, simulation::actions::UseDeveloperTool { .tool = *action, .amount = amount });
            else
                (void)use_debug_tool(device, scene, assets, tool, amount);
        }

        void tool_button(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, const char* label, DebugTool tool, i64 amount = 0)
        {
            if (ImGui::Button(label))
                pick_tool(device, scene, assets, tool, amount);
        }
    } // namespace

    usize use_debug_tool(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, DebugTool tool, i64 amount)
    {
        if (const auto action = developer_tool(tool))
        {
            // At once, as a script expects to see it.
            simulation::actions::World world = action_world(scene);
            const simulation::actions::UseDeveloperTool used { .tool = *action, .amount = amount };
            const usize changed = simulation::actions::check(world, used) ? simulation::actions::apply(world, used) : 0;
            show_changed_park(device, scene, assets);
            return changed;
        }
        const auto count = static_cast<usize>(std::max<i64>(amount, 0));
        scene.park.cheats_used = true;
        switch (tool)
        {
            case DebugTool::AddDucks:
                return simulation::add_ducks(scene.sim, bird_ground(scene), kDebugFlock);
            case DebugTool::AddGulls:
                return simulation::add_gulls(scene.sim, bird_ground(scene), kDebugGulls);
            case DebugTool::RemoveWildBirds:
                return simulation::remove_wild_birds(scene.sim);
            case DebugTool::RebuildWaterfalls:
                refresh_park_waterfalls(device, scene.waterfalls, scene.footpaths.waterfalls, scene.water.surface);
                return scene.footpaths.waterfalls.size();
            case DebugTool::ResetJets:
            {
                const usize jets = scene.jets.size();
                scene.jets.clear();
                // The jumping fountains start again at once rather than after their rest.
                scene.jumps_ended = 0;
                return jets;
            }
            case DebugTool::UnlockDesigns:
                // For this park only, as it was opened; a designs window open now shows it when opened again.
                return std::exchange(scene.designs_unlocked, true) ? 0 : 1;
            case DebugTool::UnlockScenarios:
            {
                // The career being played, else every career of every game; written at once, as nothing else
                // would save them.
                if (!scene.saves.data)
                    return 0;
                usize opened = 0;
                const std::filesystem::path folder = scene.saves.data->folder(config::UserFolder::Careers);
                for (i32 pack = 0; !career::pack_id(pack).empty(); ++pack)
                    for (const career::CareerEntry& entry : career::list_careers(folder, pack, std::nullopt))
                    {
                        if (!scene.game.career.empty() && entry.path != scene.game.career)
                            continue;
                        usize here = 0;
                        const auto opened_all = [&](career::Career& c) {
                            for (career::ScenarioProgress& s : c.scenarios)
                                if (s.status == career::ScenarioStatus::Locked)
                                {
                                    s.status = career::ScenarioStatus::Open;
                                    ++here;
                                }
                        };
                        if (career::change_career(entry.path, opened_all))
                            opened += here;
                    }
                return opened;
            }
            case DebugTool::CycleMotions:
            case DebugTool::CyclePersonMotions:
            {
                const bool everyone = tool == DebugTool::CycleMotions;
                const bool was = scene.held_motion && scene.held_motion->in_turn && scene.held_motion->everyone == everyone;
                if (amount == 0)
                {
                    if (was)
                        scene.held_motion.reset();
                    return was ? 1 : 0;
                }
                if (was || (!everyone && !scene.shown_person))
                    return 0;
                scene.held_motion = ParkScene::HeldMotion { .since = scene.clock, .in_turn = true, .everyone = everyone };
                return 1;
            }
            case DebugTool::StartDisplay:
                return start_park_display(scene, count) ? 1 : 0;
            case DebugTool::StopDisplay:
            {
                const bool playing = scene.fireworks.playing.has_value();
                stop_park_display(scene);
                return playing ? 1 : 0;
            }
            default: // The developer actions, above
                return 0;
        }
    }

    void show_changed_park(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets)
    {
        simulation::actions::Changed& changed = scene.actions.changed;
        if (std::exchange(changed.land_owned, false))
            land_owned_changed(device, scene, assets);
        if (std::exchange(changed.fences_worn, false) && fence_wear_changed(scene))
            objects_changed(scene);
        if (std::exchange(changed.invented, false))
        {
            scene.build.offer = rct3::park::BuildOffer(scene.park_model.content);
            scene.build.researching = std::nullopt;
        }
    }

    void draw_debug_tab(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets)
    {
        // What the tab's boxes hold between frames, as ImGui leaves them to the caller; one tab, one set.
        static i32 money = 10000;
        static i32 guests = 100;
        static i32 litter = 200;

        if (ImGui::CollapsingHeader("Park and money", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("##money", &money, 1000, 10000);
            ImGui::SameLine();
            ImGui::BeginDisabled(!scene.park.cash);
            tool_button(device, scene, assets, "Add money", DebugTool::AddMoney, i64 { money } * 100);
            ImGui::EndDisabled();
            tool_button(device, scene, assets, "Buy all land", DebugTool::BuyAllLand);
            ImGui::SetItemTooltip("Every tile on sale becomes the park's, for nothing");
            ImGui::SameLine();
            ImGui::BeginDisabled(!scene.research_items);
            tool_button(device, scene, assets, "Finish all research", DebugTool::FinishResearch);
            ImGui::EndDisabled();
            ImGui::BeginDisabled(scene.designs_unlocked);
            tool_button(device, scene, assets, label(assets, "Rct3pp_Debug_UnlockDesigns").c_str(), DebugTool::UnlockDesigns);
            ImGui::EndDisabled();
            ImGui::SetItemTooltip("%s", label(assets, "Rct3pp_Debug_UnlockDesignsHelp").c_str());
            ImGui::SameLine();
            ImGui::BeginDisabled(!scene.saves.data);
            tool_button(device, scene, assets, label(assets, "Rct3pp_Debug_UnlockScenarios").c_str(), DebugTool::UnlockScenarios);
            ImGui::EndDisabled();
            ImGui::SetItemTooltip("%s", label(assets, "Rct3pp_Debug_UnlockScenariosHelp").c_str());
            tool_button(device, scene, assets, label(assets, "Rct3pp_Debug_ResetItemNames").c_str(), DebugTool::ResetItemNames);
            ImGui::SetItemTooltip("%s", label(assets, "Rct3pp_Debug_ResetItemNamesHelp").c_str());
        }

        if (ImGui::CollapsingHeader("Guests", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::BeginDisabled(!scene.simulate);
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("##guests", &guests, 10, 100);
            ImGui::SameLine();
            tool_button(device, scene, assets, "Add guests", DebugTool::AddGuests, guests);
            ImGui::SetItemTooltip("Parties as the gate lets them in, spread over the park's paths");
            tool_button(device, scene, assets, "Remove all guests", DebugTool::RemoveGuests);
            ImGui::SameLine();
            tool_button(device, scene, assets, "Reset needs", DebugTool::RefreshGuests);
            ImGui::EndDisabled();
            const auto switch_box = [&](std::string_view key, std::string_view help, bool on, DebugTool tool, i64 amount_on) {
                if (ImGui::Checkbox(label(assets, key).c_str(), &on))
                    pick_tool(device, scene, assets, tool, on ? amount_on : 0);
                ImGui::SetItemTooltip("%s", label(assets, help).c_str());
            };
            constexpr i64 kSlowMotion = 4; ///< Chosen by eye: slow enough to follow a step, quick enough to see one through
            switch_box("Rct3pp_Debug_SlowGuests", "Rct3pp_Debug_SlowGuestsHelp", scene.sim.guest_slowdown > 1, DebugTool::SlowGuests, kSlowMotion);
            ImGui::SameLine();
            switch_box("Rct3pp_Debug_PartiesHuddleOften", "Rct3pp_Debug_PartiesHuddleOftenHelp", scene.sim.parties_huddle_often, DebugTool::PartiesHuddleOften, 1);
            switch_box("Rct3pp_Debug_CycleMotions", "Rct3pp_Debug_CycleMotionsHelp", scene.held_motion && scene.held_motion->in_turn && scene.held_motion->everyone, DebugTool::CycleMotions, 1);
            // The person whose window is open plays the motion picked, from its start.
            static const std::vector<rct3::park::person::NamedMotion> kMotions = rct3::park::person::motions_by_number();
            if (scene.shown_person && !kMotions.empty())
            {
                const auto at = scene.held_motion ? std::ranges::find(kMotions, scene.held_motion->motion, &rct3::park::person::NamedMotion::number) : kMotions.end();
                const usize now = at == kMotions.end() ? 0 : static_cast<usize>(at - kMotions.begin());
                const auto play = [&](usize i) { scene.held_motion = ParkScene::HeldMotion { .motion = kMotions[i].number, .since = scene.clock }; };
                if (ImGui::ArrowButton("##motion_back", ImGuiDir_Left))
                    play(scene.held_motion && now > 0 ? now - 1 : kMotions.size() - 1);
                ImGui::SameLine();
                if (ImGui::ArrowButton("##motion_on", ImGuiDir_Right))
                    play(scene.held_motion && now + 1 < kMotions.size() ? now + 1 : 0);
                ImGui::SameLine();
                switch_box("Rct3pp_Debug_CyclePersonMotions", "Rct3pp_Debug_CyclePersonMotionsHelp", scene.held_motion && scene.held_motion->in_turn && !scene.held_motion->everyone, DebugTool::CyclePersonMotions, 1);
                ImGui::SameLine();
                if (scene.held_motion && !scene.held_motion->in_turn)
                {
                    ImGui::Text("Motion %d: %.*s", scene.held_motion->motion, static_cast<int>(kMotions[now].name.size()), kMotions[now].name.data()); // c-api
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Again"))
                        play(now);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Own motions"))
                        scene.held_motion.reset();
                }
                else
                    ImGui::TextUnformatted("Step through the person's motions");
            }
            else
                ImGui::TextDisabled("Open a person's window to step through their motions");
        }

        if (ImGui::CollapsingHeader("Rides", ImGuiTreeNodeFlags_DefaultOpen))
        {
            tool_button(device, scene, assets, "Break down every ride", DebugTool::BreakRides);
            ImGui::SameLine();
            tool_button(device, scene, assets, "Reset all rides", DebugTool::ResetRides);
            ImGui::SetItemTooltip("Repaired, fully reliable, queues emptied and their counts back to nought");
        }

        if (ImGui::CollapsingHeader("Animals", ImGuiTreeNodeFlags_DefaultOpen))
        {
            tool_button(device, scene, assets, "Remove all animals", DebugTool::RemoveAnimals);
            ImGui::SameLine();
            tool_button(device, scene, assets, "Reset animals' needs", DebugTool::RefreshAnimals);
            tool_button(device, scene, assets, "Wear enclosure fences", DebugTool::KnockFences, kDebugFenceKnocks);
            ImGui::SetItemTooltip("Knocks the fences of random enclosures; a panel breaks at its second knock");
            ImGui::SameLine();
            tool_button(device, scene, assets, "Make females pregnant", DebugTool::BreedAnimals);
            ImGui::SetItemTooltip("Each adult female gives birth to one young an hour from now");
            ImGui::SameLine();
            tool_button(device, scene, assets, "Age animals a year", DebugTool::AgeAnimals);
        }

        if (ImGui::CollapsingHeader("Wild birds", ImGuiTreeNodeFlags_DefaultOpen))
        {
            tool_button(device, scene, assets, "Add a flock of ducks", DebugTool::AddDucks);
            ImGui::SameLine();
            tool_button(device, scene, assets, "Add gulls", DebugTool::AddGulls);
            ImGui::SameLine();
            tool_button(device, scene, assets, "Remove all wild birds", DebugTool::RemoveWildBirds);
        }

        if (ImGui::CollapsingHeader("Litter", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("##litter", &litter, 10, 100);
            ImGui::SameLine();
            tool_button(device, scene, assets, "Drop litter", DebugTool::DropLitter, litter);
            ImGui::SameLine();
            tool_button(device, scene, assets, "Clear all litter", DebugTool::ClearLitter);
        }

        if (ImGui::CollapsingHeader("Time and weather", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const i32 weather = std::to_underlying(scene.park.weather);
            if (ImGui::BeginCombo("Weather", save::model::to_string(scene.park.weather)))
            {
                for (i32 raw = 0; raw <= std::to_underlying(save::model::WeatherType::Snowstorm); ++raw)
                {
                    const save::model::WeatherType type = save::model::weather_type_from(raw);
                    if (ImGui::Selectable(save::model::to_string(type), raw == weather))
                        simulation::actions::ask(scene.actions, scene.sim, simulation::actions::SetParkValues { .weather = type });
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip("Holds till the next forecast; the Disable weather option holds it for good");
            f32 hour = scene.day_clock.time * 24;
            if (ImGui::SliderFloat("Time of day", &hour, 0, 24, "%.1f h"))
                simulation::actions::ask(scene.actions, scene.sim, simulation::actions::SetParkValues { .time_of_day = hour / 24 });
            bool rain = scene.rain_strength.has_value();
            if (ImGui::Checkbox("Set rain", &rain))
                scene.rain_strength = rain ? std::optional(scene.weather_look.rain) : std::nullopt;
            ImGui::SameLine();
            ImGui::BeginDisabled(!rain);
            f32 strength = scene.rain_strength.value_or(scene.weather_look.rain);
            if (ImGui::SliderFloat("Rain strength", &strength, 0, 1) && rain)
                scene.rain_strength = strength;
            ImGui::EndDisabled();
        }

        if (ImGui::CollapsingHeader("Water and display", ImGuiTreeNodeFlags_DefaultOpen))
        {
            tool_button(device, scene, assets, "Rebuild waterfalls", DebugTool::RebuildWaterfalls);
            ImGui::SameLine();
            tool_button(device, scene, assets, "Reset water jets", DebugTool::ResetJets);
            ImGui::Checkbox("Interface", &scene.show_interface);
            ImGui::SameLine();
            ImGui::Checkbox("Ground", &scene.show_ground);
            ImGui::SetItemTooltip("The land and the paths on it; the render statistics are in the Profiler");
        }

        if (ImGui::CollapsingHeader("Firework displays", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const ParkFireworks& fireworks = scene.fireworks;
            if (fireworks.displays.empty())
                ImGui::TextDisabled("The park has no displays");
            for (usize i = 0; i < fireworks.displays.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i)); // c-api
                tool_button(device, scene, assets, "Start", DebugTool::StartDisplay, static_cast<i64>(i));
                ImGui::SameLine();
                const bool playing = fireworks.playing && fireworks.playing->display == i;
                ImGui::Text("%s%s", fireworks.displays[i].name.c_str(), playing ? " (playing)" : "");
                ImGui::PopID();
            }
            ImGui::BeginDisabled(!fireworks.playing);
            tool_button(device, scene, assets, "Stop the display", DebugTool::StopDisplay);
            ImGui::EndDisabled();
        }
    }

} // namespace rct3::scene::park
