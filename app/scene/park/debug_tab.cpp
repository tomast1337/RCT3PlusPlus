// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park/debug_tab.hpp"

#include "imgui.h"
#include "park/boundary_fence.hpp"
#include "park/land_buying.hpp"
#include "park/person/motions.hpp"
#include "scene/land_tools.hpp"
#include "scene/park/scene.hpp"
#include "simulation/debug_tools.hpp"
#include "simulation/guests.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace rct3::scene::park
{

    namespace
    {
        /// The park's own walkable tiles, not its queues nor the road out to the gate: where guests and litter are
        /// dropped.
        std::vector<simulation::NodeId> park_paths(const ParkScene& scene)
        {
            std::vector<simulation::NodeId> spots;
            const auto& nodes = scene.sim.paths.nodes();
            for (std::uint32_t n = 0; n < nodes.size(); ++n)
                if (const auto& t = nodes[n].tile; !nodes[n].queue && !nodes[n].removed && scene.grid.contains(t.col, t.row) && rct3::park::owned_outright(scene.grid.at(t.col, t.row)))
                    spots.push_back(simulation::NodeId { n });
            return spots;
        }

        void tool_button(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, const char* label, DebugTool tool, std::int64_t amount = 0)
        {
            if (ImGui::Button(label))
                (void)use_debug_tool(device, scene, assets, tool, amount);
        }
    } // namespace

    std::size_t use_debug_tool(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, DebugTool tool, std::int64_t amount)
    {
        const auto count = static_cast<std::size_t>(std::max<std::int64_t>(amount, 0));
        switch (tool)
        {
            case DebugTool::AddMoney:
                if (!scene.park.cash)
                    return 0;
                *scene.park.cash += units::Money { amount };
                return 1;
            case DebugTool::BuyAllLand:
            {
                std::vector<save::model::Tile> tiles;
                for (int row = 0; row < scene.grid.height; ++row)
                    for (int col = 0; col < scene.grid.width; ++col)
                        tiles.push_back({ col, row });
                // Free: the tool is for setting a park up, not for its books.
                const std::size_t bought = rct3::park::buy_land(scene.grid, tiles, rct3::park::LandBuy::Land, { }, std::nullopt).tiles + rct3::park::buy_land(scene.grid, tiles, rct3::park::LandBuy::Rights, { }, std::nullopt).tiles;
                if (bought > 0)
                    land_owned_changed(scene, assets);
                return bought;
            }
            case DebugTool::FinishResearch:
            {
                // The list of what is left comes once the build lists are read.
                if (!scene.research_items)
                    return 0;
                const std::size_t had = scene.research.invented.size();
                simulation::invent_everything(scene.research, *scene.research_items, scene.park.time);
                for (std::size_t i = had; i < scene.research.invented.size(); ++i)
                    if (const auto entry = std::ranges::find(scene.park_model.content.entries, scene.research.invented[i], &save::model::AssetRequirement::id); entry != scene.park_model.content.entries.end())
                        entry->invented = true;
                scene.build.offer = rct3::park::BuildOffer(scene.park_model.content);
                scene.build.researching = std::nullopt;
                return scene.research.invented.size() - had;
            }
            case DebugTool::AddGuests:
                return simulation::add_guests(scene.sim, scene.gameplay, park_paths(scene), count);
            case DebugTool::RemoveGuests:
                return simulation::send_guests_home(scene.sim);
            case DebugTool::RefreshGuests:
                return simulation::refresh_guests(scene.sim);
            case DebugTool::BreakRides:
                return simulation::break_down_rides(scene.sim);
            case DebugTool::ResetRides:
                return simulation::reset_rides(scene.sim);
            case DebugTool::RemoveAnimals:
                return simulation::remove_animals(scene.sim);
            case DebugTool::RefreshAnimals:
                return simulation::refresh_animals(scene.sim);
            case DebugTool::DropLitter:
                return simulation::scatter_litter(scene.sim, scene.gameplay, park_paths(scene), count);
            case DebugTool::ClearLitter:
                return simulation::clear_litter(scene.sim);
        }
        return 0;
    }

    void draw_debug_tab(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets)
    {
        // What the tab's boxes hold between frames, as ImGui leaves them to the caller; one tab, one set.
        static int money = 10000;
        static int guests = 100;
        static int litter = 200;

        if (ImGui::CollapsingHeader("Park and money", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("##money", &money, 1000, 10000);
            ImGui::SameLine();
            ImGui::BeginDisabled(!scene.park.cash);
            tool_button(device, scene, assets, "Add money", DebugTool::AddMoney, std::int64_t { money } * 100);
            ImGui::EndDisabled();
            tool_button(device, scene, assets, "Buy all land", DebugTool::BuyAllLand);
            ImGui::SetItemTooltip("Every tile on sale becomes the park's, for nothing");
            ImGui::SameLine();
            ImGui::BeginDisabled(!scene.research_items);
            tool_button(device, scene, assets, "Finish all research", DebugTool::FinishResearch);
            ImGui::EndDisabled();
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
            // The person whose window is open plays the motion picked, from its start.
            static const std::vector<rct3::park::person::NamedMotion> kMotions = rct3::park::person::motions_by_number();
            if (scene.shown_person && !kMotions.empty())
            {
                const auto at = scene.held_motion ? std::ranges::find(kMotions, scene.held_motion->motion, &rct3::park::person::NamedMotion::number) : kMotions.end();
                const std::size_t now = at == kMotions.end() ? 0 : static_cast<std::size_t>(at - kMotions.begin());
                const auto play = [&](std::size_t i) { scene.held_motion = ParkScene::HeldMotion { .motion = kMotions[i].number, .since = scene.clock }; };
                if (ImGui::ArrowButton("##motion_back", ImGuiDir_Left))
                    play(scene.held_motion && now > 0 ? now - 1 : kMotions.size() - 1);
                ImGui::SameLine();
                if (ImGui::ArrowButton("##motion_on", ImGuiDir_Right))
                    play(scene.held_motion && now + 1 < kMotions.size() ? now + 1 : 0);
                ImGui::SameLine();
                if (scene.held_motion)
                {
                    ImGui::Text("Motion %d: %.*s", scene.held_motion->motion, static_cast<int>(kMotions[now].name.size()), kMotions[now].name.data());
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
            const std::int32_t weather = std::to_underlying(scene.park.weather);
            if (ImGui::BeginCombo("Weather", save::model::to_string(scene.park.weather)))
            {
                for (std::int32_t raw = 0; raw <= std::to_underlying(save::model::WeatherType::Snowstorm); ++raw)
                {
                    const save::model::WeatherType type = save::model::weather_type_from(raw);
                    if (ImGui::Selectable(save::model::to_string(type), raw == weather))
                    {
                        scene.park.weather = type;
                        scene.park.forecast = type;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip("Holds till the next forecast; the Disable weather option holds it for good");
            float hour = scene.day_clock.time * 24;
            if (ImGui::SliderFloat("Time of day", &hour, 0, 24, "%.1f h"))
            {
                // Shown at once, without a blend; the clock runs on from there, into the park's hours if outside.
                scene.day_clock.time = scene.day_clock.shown = hour / 24;
                scene.day_clock.fade = 0;
            }
            bool rain = scene.rain_strength.has_value();
            if (ImGui::Checkbox("Set rain", &rain))
                scene.rain_strength = rain ? std::optional(scene.weather_look.rain) : std::nullopt;
            ImGui::SameLine();
            ImGui::BeginDisabled(!rain);
            float strength = scene.rain_strength.value_or(scene.weather_look.rain);
            if (ImGui::SliderFloat("Rain strength", &strength, 0, 1) && rain)
                scene.rain_strength = strength;
            ImGui::EndDisabled();
        }
    }

} // namespace rct3::scene::park
