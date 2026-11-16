// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park/debug_tab.hpp"

#include "imgui.h"
#include "park/boundary_fence.hpp"
#include "park/land_buying.hpp"
#include "scene/land_tools.hpp"
#include "scene/park/scene.hpp"
#include "simulation/debug_tools.hpp"

#include <algorithm>
#include <vector>

namespace rct3::scene::park
{

    namespace
    {
        void tool_button(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, const char* label, DebugTool tool, std::int64_t amount = 0)
        {
            if (ImGui::Button(label))
                (void)use_debug_tool(device, scene, assets, tool, amount);
        }
    } // namespace

    std::size_t use_debug_tool(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets, DebugTool tool, std::int64_t amount)
    {
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
        }
        return 0;
    }

    void draw_debug_tab(render::Device& device, ParkScene& scene, const ui::InterfaceAssets& assets)
    {
        // What the tab's boxes hold between frames, as ImGui leaves them to the caller; one tab, one set.
        static int money = 10000;

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
    }

} // namespace rct3::scene::park
