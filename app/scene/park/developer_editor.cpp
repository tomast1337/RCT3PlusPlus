// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park/developer_editor.hpp"

#include "imgui.h"
#include "park/camera.hpp"
#include "scene/park/scene.hpp"
#include "simulation/day_clock.hpp"
#include "simulation/guest_edits.hpp"
#include "simulation/guests.hpp"
#include "simulation/parties.hpp"
#include "simulation/systems.hpp"
#include "strings/unicode.hpp"
#include "ui/thought_text.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <string_view>
#include <utility>

namespace rct3::scene::park
{

    namespace
    {

        constexpr std::array<std::string_view, 8> kErrands { "wandering", "heading", "queueing", "riding", "following", "waiting", "buying", "swimming" };
        constexpr std::array<std::string_view, 3> kAges { "child", "teen", "adult" };

        std::string guest_name(const entt::registry& r, entt::entity guest)
        {
            const std::string& name = r.get<const simulation::Visitor>(guest).name;
            return name.empty() ? std::format("Guest {}", entt::to_integral(guest)) : name;
        }

        std::string_view attraction_name(const entt::registry& r, entt::entity e)
        {
            if (!r.valid(e))
                return "";
            if (const auto* a = r.try_get<const simulation::Attraction>(e))
                return a->name;
            return r.all_of<simulation::ParkGate>(e) ? "the gate" : r.all_of<simulation::GuestSpawn>(e) ? "the road out" : "";
        }

        std::string doing(const entt::registry& r, entt::entity guest)
        {
            const auto* errand = r.try_get<const simulation::Errand>(guest);
            if (errand == nullptr)
                return r.all_of<simulation::Held>(guest) ? "held by the player" : "off the paths";
            const std::string_view at = attraction_name(r, errand->target);
            return at.empty() ? std::string(kErrands[std::to_underlying(errand->kind)]) : std::format("{} {}", kErrands[std::to_underlying(errand->kind)], at);
        }

        /// Money as an input box in currency units; true when changed.
        bool money_input(const char* label, units::Money& money)
        {
            double units = static_cast<double>(money.value) / 100;
            if (!ImGui::InputDouble(label, &units, 1, 100, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue))
                return false;
            money.value = std::llround(units * 100);
            return true;
        }

        /// A level's slider and its hold switch.
        void level_row(entt::registry& r, entt::entity guest, simulation::GuestLevel level)
        {
            const auto now = simulation::level_of(r, guest, level);
            if (!now)
                return;
            const std::string_view name = simulation::level_name(level);
            ImGui::PushID(static_cast<int>(level));
            bool held = simulation::level_held(r, guest, level);
            if (ImGui::Checkbox("hold", &held))
                simulation::hold_level(r, guest, level, held);
            ImGui::SetItemTooltip("Kept at this level while the park runs on; not saved with the park");
            ImGui::SameLine();
            float value = now->value;
            if (ImGui::SliderFloat(std::string(name).c_str(), &value, 0, 1, "%.2f"))
                (void)simulation::set_level(r, guest, level, { value });
            ImGui::PopID();
        }

        void draw_guest_list(ParkScene& scene)
        {
            DeveloperEditor& ed = scene.editor;
            auto& r = scene.sim.registry;
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##search", "Search by name", ed.search.data(), ed.search.size());
            const std::u16string wanted = strings::search_folded(strings::to_utf16(ed.search.data()));
            ed.rows.clear();
            for (const entt::entity e : r.view<const simulation::Visitor, const simulation::Mood>(entt::exclude<simulation::Left>))
                if (wanted.empty() || strings::search_folded(strings::to_utf16(guest_name(r, e))).contains(wanted))
                    ed.rows.push_back(e);
            enum Column : int
            {
                Name,
                Age,
                Doing,
                Happiness,
                Energy,
                Nausea,
                Hunger,
                Thirst,
                Cash,
            };
            constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV;
            if (!ImGui::BeginTable("guests", 9, kFlags, { 0, ImGui::GetTextLineHeightWithSpacing() * 8 }))
                return;
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch);
            for (const char* column : { "Age", "Doing", "Happy", "Energy", "Nausea", "Hunger", "Thirst", "Cash" })
                ImGui::TableSetupColumn(column, column == std::string_view("Doing") ? ImGuiTableColumnFlags_WidthStretch : ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableHeadersRow();
            if (const ImGuiTableSortSpecs* sort = ImGui::TableGetSortSpecs(); sort != nullptr && sort->SpecsCount > 0)
            {
                const auto column = static_cast<Column>(sort->Specs[0].ColumnIndex);
                const bool descending = sort->Specs[0].SortDirection == ImGuiSortDirection_Descending;
                const auto level = [&](entt::entity e, simulation::GuestLevel l) { return simulation::level_of(r, e, l).value_or(units::Share { }).value; };
                const auto less = [&](entt::entity a, entt::entity b) {
                    switch (column)
                    {
                        case Name:
                            return guest_name(r, a) < guest_name(r, b);
                        case Age:
                            return r.get<simulation::Visitor>(a).age < r.get<simulation::Visitor>(b).age;
                        case Doing:
                            return doing(r, a) < doing(r, b);
                        case Happiness:
                            return level(a, simulation::GuestLevel::Happiness) < level(b, simulation::GuestLevel::Happiness);
                        case Energy:
                            return level(a, simulation::GuestLevel::Energy) < level(b, simulation::GuestLevel::Energy);
                        case Nausea:
                            return level(a, simulation::GuestLevel::Nausea) < level(b, simulation::GuestLevel::Nausea);
                        case Hunger:
                            return level(a, simulation::GuestLevel::Hunger) < level(b, simulation::GuestLevel::Hunger);
                        case Thirst:
                            return level(a, simulation::GuestLevel::Thirst) < level(b, simulation::GuestLevel::Thirst);
                        case Cash:
                            return r.get<simulation::Wallet>(a).cash < r.get<simulation::Wallet>(b).cash;
                    }
                    return a < b;
                };
                std::ranges::stable_sort(ed.rows, [&](entt::entity a, entt::entity b) { return descending ? less(b, a) : less(a, b); });
            }
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(ed.rows.size()));
            while (clipper.Step())
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                {
                    const entt::entity e = ed.rows[static_cast<std::size_t>(row)];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::PushID(static_cast<int>(entt::to_integral(e)));
                    if (ImGui::Selectable(guest_name(r, e).c_str(), e == ed.guest, ImGuiSelectableFlags_SpanAllColumns))
                        pick_guest(scene, e);
                    ImGui::PopID();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(kAges[std::to_underlying(r.get<simulation::Visitor>(e).age)].data());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(doing(r, e).c_str());
                    for (const auto level : { simulation::GuestLevel::Happiness, simulation::GuestLevel::Energy, simulation::GuestLevel::Nausea, simulation::GuestLevel::Hunger, simulation::GuestLevel::Thirst })
                    {
                        ImGui::TableNextColumn();
                        ImGui::Text("%.2f", static_cast<double>(simulation::level_of(r, e, level).value_or(units::Share { }).value));
                    }
                    ImGui::TableNextColumn();
                    if (const auto* wallet = r.try_get<simulation::Wallet>(e))
                        ImGui::Text("%.2f", static_cast<double>(wallet->cash.value) / 100);
                }
            ImGui::EndTable();
        }

        void draw_guest_page(ParkScene& scene, const ui::StringTable& strings)
        {
            DeveloperEditor& ed = scene.editor;
            auto& r = scene.sim.registry;
            const entt::entity guest = ed.guest;
            if (!r.valid(guest) || !r.all_of<simulation::Visitor>(guest))
            {
                ImGui::TextDisabled("Pick a guest in the list");
                return;
            }
            const simulation::Visitor& visitor = r.get<simulation::Visitor>(guest);
            const ShownPerson* shown = nullptr;
            for (const ShownPerson& p : scene.shown_people)
                if (!p.staff && p.guest == guest)
                    shown = &p;
            const save::model::Guest* page = shown != nullptr && shown->saved < scene.people.guests.size() ? &scene.people.guests[shown->saved] : nullptr;
            const bool gone = r.all_of<simulation::Left>(guest);

            if (ImGui::CollapsingHeader("Overview", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::SetNextItemWidth(200);
                ImGui::InputText("##name", ed.name.data(), ed.name.size());
                ImGui::SameLine();
                if (ImGui::Button("Rename") && ed.name[0] != '\0')
                {
                    // The simulation's name is saved; the page's is what the game's windows show.
                    r.get<simulation::Visitor>(guest).name = ed.name.data();
                    if (shown != nullptr && shown->saved < scene.people.guests.size())
                        scene.people.guests[shown->saved].person.name = ed.name.data();
                }
                ImGui::SameLine();
                if (ImGui::Button("Look at") && r.all_of<simulation::Pose>(guest))
                    scene.camera.target = r.get<simulation::Pose>(guest).position;
                ImGui::Text("%s %s%s%s", visitor.female ? "female" : "male", kAges[std::to_underlying(visitor.age)].data(), visitor.avatar ? ", the player's own" : "", gone ? ", gone home" : "");
                if (const auto* member = r.try_get<simulation::Member>(guest); member != nullptr && r.valid(member->party))
                {
                    const simulation::Party& party = r.get<simulation::Party>(member->party);
                    ImGui::Text("In a party of %zu%s", party.members.size(), party.leader == guest ? ", its leader" : "");
                }
                else
                    ImGui::TextUnformatted("On their own");
                ImGui::Text("Doing: %s", doing(r, guest).c_str());
                if (const auto* pose = r.try_get<simulation::Pose>(guest))
                {
                    const save::model::TerrainGrid& g = scene.grid;
                    ImGui::Text("At tile %.0f, %.0f", std::floor((pose->position.x - g.origin_x) / g.tile_size_x), std::floor((pose->position.z - g.origin_z) / g.tile_size_z));
                }
                const float hours = static_cast<float>(scene.sim.tick - std::min(scene.sim.tick, visitor.arrived)) * simulation::kTickSeconds / simulation::kSecondsPerHour;
                ImGui::Text("In the park %.1f hours (since the park was opened here)", static_cast<double>(hours));
                if (page != nullptr)
                    ImGui::Text("Rides %d, food %d, drinks %d, souvenirs %d (as their page keeps them)", page->counters.rides, page->counters.food_items, page->counters.drinks, page->counters.souvenirs);
                std::string carries;
                if (const auto* carried = r.try_get<simulation::Carried>(guest))
                    carries += carried->bites > 0 ? (carried->food ? " food," : " a drink,") : " rubbish,";
                if (r.all_of<simulation::Umbrella>(guest))
                    carries += " an umbrella,";
                if (r.all_of<simulation::ParkMap>(guest))
                    carries += " a map,";
                if (const auto* passport = r.try_get<simulation::Passport>(guest))
                    carries += std::format(" a passport with {} stamps,", passport->stamps.size());
                if (!carries.empty())
                    carries.pop_back();
                ImGui::Text("Carrying:%s", carries.empty() ? " nothing" : carries.c_str());
            }
            if (gone)
                return;
            if (ImGui::CollapsingHeader("Money"))
            {
                if (auto* wallet = r.try_get<simulation::Wallet>(guest))
                {
                    ImGui::SetNextItemWidth(160);
                    (void)money_input("Cash", wallet->cash);
                    ImGui::SetNextItemWidth(160);
                    (void)money_input("Bank", wallet->bank);
                    const auto at = [](units::Money m) { return static_cast<double>(m.value) / 100; };
                    ImGui::Text("Spent %.2f: entry %.2f, rides %.2f, food %.2f, drink %.2f, souvenirs %.2f", at(wallet->spent), at(wallet->spent_entry), at(wallet->spent_rides), at(wallet->spent_food), at(wallet->spent_drink), at(wallet->spent_souvenirs));
                }
            }
            if (ImGui::CollapsingHeader("Feelings and needs", ImGuiTreeNodeFlags_DefaultOpen))
            {
                for (std::size_t k = 0; k < simulation::kGuestLevels; ++k)
                    if (static_cast<simulation::GuestLevel>(k) != simulation::GuestLevel::Tolerance)
                        level_row(r, guest, static_cast<simulation::GuestLevel>(k));
            }
            if (ImGui::CollapsingHeader("Preferences"))
            {
                simulation::Taste& taste = r.get_or_emplace<simulation::Taste>(guest);
                ImGui::DragFloatRange2("intensity", &taste.least.value, &taste.most.value, 0.01f, 0, 1, "%.2f");
                ImGui::SliderFloat("pools", &taste.pools.value, 0, 1, "%.2f");
                level_row(r, guest, simulation::GuestLevel::Tolerance);
            }
            if (ImGui::CollapsingHeader("Route"))
            {
                if (const auto* stride = r.try_get<simulation::Stride>(guest))
                {
                    const auto tile = [&](simulation::NodeId n) {
                        if (n.value >= scene.sim.paths.nodes().size())
                            return std::string("off the paths");
                        const glm::vec3 c = scene.sim.paths.centre(n);
                        return std::format("{:.0f},{:.0f}", std::floor((c.x - scene.grid.origin_x) / scene.grid.tile_size_x), std::floor((c.z - scene.grid.origin_z) / scene.grid.tile_size_z));
                    };
                    ImGui::Text("Stepping from %s to %s, %zu tiles left after", tile(stride->from).c_str(), tile(stride->to).c_str(), stride->route.size());
                    std::string walk;
                    for (const simulation::NodeId n : stride->route)
                        walk += std::format("{}  ", tile(n));
                    ImGui::TextWrapped("%s", walk.c_str());
                }
                else
                    ImGui::TextDisabled("Not walking the paths");
            }
            if (ImGui::CollapsingHeader("Actions", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (ImGui::Button("Make happy and healthy"))
                    simulation::make_happy_and_healthy(r, guest);
                ImGui::SameLine();
                if (ImGui::Button("Cure nausea"))
                    simulation::cure_nausea(r, guest);
                ImGui::SameLine();
                if (ImGui::Button("Send home"))
                    (void)simulation::send_home(scene.sim, guest);
                ImGui::SetItemTooltip("With their party; only while they walk the paths");

                if (ed.thoughts.empty())
                    for (std::int32_t id = 1; id < 256; ++id)
                        if (const std::u16string words = ui::thought_text(strings, id, 0, u"..."); !words.empty())
                            ed.thoughts.push_back({ .id = id, .text = std::format("{}: {}", id, strings::to_utf8(words)) });
                const auto thought = std::ranges::find(ed.thoughts, ed.thought, &DeveloperEditor::ThoughtText::id);
                ImGui::SetNextItemWidth(260);
                if (ImGui::BeginCombo("##thought", thought != ed.thoughts.end() ? thought->text.c_str() : "a thought"))
                {
                    for (const auto& t : ed.thoughts)
                        if (ImGui::Selectable(t.text.c_str(), t.id == ed.thought))
                            ed.thought = t.id;
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                if (ImGui::Button("Give thought") && thought != ed.thoughts.end() && r.all_of<simulation::Dice>(guest))
                    simulation::think(r, guest, static_cast<simulation::ThoughtKind>(ed.thought), entt::null, static_cast<float>(scene.sim.tick) * simulation::kTickSeconds);

                ImGui::SetNextItemWidth(260);
                if (ImGui::BeginCombo("##send", r.valid(ed.send_to) ? std::string(attraction_name(r, ed.send_to)).c_str() : "a ride or shop"))
                {
                    for (const auto [e, attraction] : r.view<const simulation::Attraction>().each())
                        if (attraction.entrance)
                        {
                            ImGui::PushID(static_cast<int>(entt::to_integral(e)));
                            if (ImGui::Selectable(attraction.name.c_str(), e == ed.send_to))
                                ed.send_to = e;
                            ImGui::PopID();
                        }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                if (ImGui::Button("Send there"))
                    (void)simulation::send_to(scene.sim, guest, ed.send_to);
                ImGui::SetItemTooltip("With their party; only while they walk the paths");
            }
        }

    } // namespace

    void pick_guest(ParkScene& scene, entt::entity guest)
    {
        DeveloperEditor& ed = scene.editor;
        ed.guest = guest;
        ed.focus = true;
        ed.name = { };
        if (const auto* visitor = scene.sim.registry.valid(guest) ? scene.sim.registry.try_get<simulation::Visitor>(guest) : nullptr)
            visitor->name.copy(ed.name.data(), std::min(visitor->name.size(), ed.name.size() - 1));
    }

    void draw_developer_editor(ParkScene& scene, const ui::StringTable& strings)
    {
        scene.editor.shown = true;
        if (!ImGui::BeginTabBar("editor"))
            return;
        if (ImGui::BeginTabItem("Guests"))
        {
            draw_guest_list(scene);
            ImGui::Separator();
            ImGui::BeginChild("guest");
            draw_guest_page(scene, strings);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    void mark_picked_guest(const ParkScene& scene, const render::ScreenView& view)
    {
        const auto& r = scene.sim.registry;
        if (!scene.editor.shown || !r.valid(scene.editor.guest) || !r.all_of<simulation::Pose>(scene.editor.guest) || view.size.width == 0 || view.size.height == 0)
            return;
        constexpr float kOverHead = 2.2f; ///< World units over their feet the marker points down at, chosen by eye
        const glm::vec3 feet = r.get<simulation::Pose>(scene.editor.guest).position;
        const glm::vec4 clip = rct3::park::view_projection(scene.camera, static_cast<float>(view.size.width) / static_cast<float>(view.size.height)) * glm::vec4(feet + glm::vec3(0, kOverHead, 0), 1);
        if (clip.w <= 0)
            return;
        const ImVec2 to_ui = ImGui::GetIO().DisplayFramebufferScale;
        const ImVec2 at { (static_cast<float>(view.x) + (clip.x / clip.w + 1) / 2 * static_cast<float>(view.size.width)) / to_ui.x, (static_cast<float>(view.y) + (1 - clip.y / clip.w) / 2 * static_cast<float>(view.size.height)) / to_ui.y };
        ImDrawList& draw = *ImGui::GetForegroundDrawList();
        draw.AddTriangleFilled({ at.x - 9, at.y - 16 }, { at.x + 9, at.y - 16 }, at, IM_COL32(255, 200, 40, 230));
        draw.AddTriangle({ at.x - 9, at.y - 16 }, { at.x + 9, at.y - 16 }, at, IM_COL32(20, 20, 20, 255), 1.5f);
    }

} // namespace rct3::scene::park
