// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park's info panel: opened on an attraction, a prop, a person or the party they go round with, its pages
// written from the park as it stands, and the changes the player makes on an attraction's or a prop's page
// written into the save. A person's page writes its changes through park/people.hpp.
#pragma once

#include "config/game/options.hpp"
#include "park/attraction_objects.hpp"
#include "park/camera.hpp"
#include "render/device.hpp"
#include "save/model/world.hpp"
#include "simulation/entt.hpp"
#include "ui/interface_assets.hpp"
#include "ui/screens/attraction_info.hpp"
#include "ui/screens/info_panel.hpp"
#include "ui/screens/management_pages.hpp"
#include "ui/screens/scenery_info.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace rct3::scene::park
{

    struct ParkScene;

    /// What the info panel shows of attraction `ref`.
    [[nodiscard]] ui::screens::AttractionFacts facts(const ParkScene& scene, rct3::park::AttractionRef ref);

    /// The money and temperature units the info panel shows values in, as the options have them.
    [[nodiscard]] ui::screens::InfoUnits info_units(const ui::InterfaceAssets& assets, const config::game::GameOptions& options);

    /// The changing room (of the pools') the object `o` draws, if it draws one.
    [[nodiscard]] std::optional<std::size_t> pool_room(const ParkScene& scene, const save::model::PlacedObject& o);

    /// The simulated changing room of `room`, if the simulation has it.
    [[nodiscard]] entt::entity room_entity(const ParkScene& scene, std::size_t room);

    /// What the info panel shows of the prop `index` (of the objects).
    [[nodiscard]] ui::screens::SceneryFacts prop_facts(const ParkScene& scene, std::size_t index);

    /// Adds the pictures of what stalls sell, for their menus, to the interface's.
    void load_carried(render::Device& device, ParkScene& scene, ui::InterfaceAssets& assets);

    /// Shows the page of what the panel is open on again, as it stands now: a party, a prop, a person or an
    /// attraction.
    void show_info_page(ParkScene& scene, ui::screens::InfoPanel& panel, const ui::InterfaceAssets& assets, const config::game::GameOptions& options);

    /// The round view of a person: looking at them from where the player looks from, turning from there.
    [[nodiscard]] rct3::park::ParkCamera person_view(const ParkScene& scene, std::size_t shown, float yaw);

    /// Opens the panel on the person `shown` (of the shown people), at the tab last opened on a person if
    /// they have it.
    void open_person(ParkScene& scene, ui::screens::InfoPanel& panel, std::size_t shown, const ui::InterfaceAssets& assets, const config::game::GameOptions& options);

    /// Opens the panel on the party shown person `shown` goes round with, at the tab last opened on a party.
    void open_party(ParkScene& scene, ui::screens::InfoPanel& panel, std::size_t shown, const ui::InterfaceAssets& assets, const config::game::GameOptions& options);

    /// Opens the panel on the attraction `ref`, at `tab`, else the tab last opened, if it has one.
    void open_info(ParkScene& scene, ui::screens::InfoPanel& panel, rct3::park::AttractionRef ref, const ui::InterfaceAssets& assets, const config::game::GameOptions& options, std::optional<ui::screens::AttractionTab> tab = std::nullopt);

    /// Opens the panel on the prop `index` (of the objects), with only its name showing, or on its tab `open`.
    void open_prop(ParkScene& scene, ui::screens::InfoPanel& panel, std::size_t index, const ui::InterfaceAssets& assets, const config::game::GameOptions& options, std::optional<ui::screens::SceneryTab> open = std::nullopt);

    /// Paints colour slot `slot` of the prop `index` in colour `colour` of the 32.
    void paint_prop(render::Device& device, ParkScene& scene, std::size_t index, std::size_t slot, std::int32_t colour);

    /// Writes a change the player made on an attraction's page into the save. Nothing is shown again, so a
    /// spinner held down keeps stepping; a page that looks different after the change is set up here.
    void change_attraction(render::Device& device, ParkScene& scene, ui::screens::InfoPanel& panel, rct3::park::AttractionRef ref, const ui::screens::InfoChange& change, const ui::InterfaceAssets& assets, const config::game::GameOptions& options);

    /// Opens the info panel on what a management window's row is about, if the park shows it.
    void open_row(ParkScene& scene, ui::screens::InfoPanel& panel, ui::screens::ManagementRow row, const ui::InterfaceAssets& assets, const config::game::GameOptions& options);

} // namespace rct3::scene::park
