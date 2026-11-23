// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/park/animal_sale.hpp"

#include "scene/park/scene.hpp"
#include "scene/park/setup.hpp"
#include "simulation/animal_sale.hpp"
#include "strings/ascii.hpp"
#include "ui/game_style.hpp"
#include "ui/screens/info/panel.hpp"
#include "ui/widgets/question_box.hpp"

#include <chrono>
#include <future>
#include <string>
#include <vector>

namespace rct3::scene::park
{

    namespace
    {
        constexpr std::string_view kSellText = "SellAnimalConfirmMessage";
        constexpr std::u16string_view kAmountMark = u"%AMOUNT%";
        constexpr std::string_view kSoldText = "SellAnimalSoldMessage";
        constexpr std::string_view kYes = "SMB_Label_Yes";
        constexpr std::string_view kNo = "SMB_Label_No";
    } // namespace

    std::optional<units::Money> animal_buy_price(const ParkScene& scene, const simulation::Animal& animal)
    {
        using ovl::animal::AnimalAge;
        if (!scene.build.items.valid() || scene.build.items.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return std::nullopt;
        const AnimalAge age = animal.adult ? (animal.male ? AnimalAge::AdultMale : AnimalAge::AdultFemale) : (animal.male ? AnimalAge::YoungMale : AnimalAge::YoungFemale);
        for (const rct3::park::BuildItem& item : scene.build.items.get())
            if (item.kind == rct3::park::BuildKind::Animal && item.age == age && strings::equals_ignoring_case(item.symbol, animal.species))
                return item.price;
        return std::nullopt;
    }

    void ask_sell_animal(ParkScene& scene, const ui::InterfaceAssets& assets, const ui::Currency& currency, entt::entity animal)
    {
        const auto* sold = scene.sim.registry.valid(animal) ? scene.sim.registry.try_get<simulation::Animal>(animal) : nullptr;
        const std::optional<units::Money> price = sold != nullptr ? animal_buy_price(scene, *sold) : std::nullopt;
        if (!price || sold->job == simulation::AnimalJob::Crated || scene.desktop->root().find(kSellAnimalName))
            return;
        std::u16string text(assets.strings->get(kSellText));
        if (const auto at = text.find(kAmountMark); at != std::u16string::npos)
            text.replace(at, kAmountMark.size(), ui::money_text(currency, simulation::sale_price(*price, sold->health)));
        const ui::widgets::QuestionBoxStyle style = ui::game_skins::question_box_style(assets.palette);
        scene.desktop->root().add<ui::widgets::QuestionBox>(std::string(kSellAnimalName), style, std::move(text), std::vector<ui::widgets::QuestionAnswer> { { std::u16string(assets.strings->get(kYes)), style.confirm_icon }, { std::u16string(assets.strings->get(kNo)), style.cancel_icon } });
        scene.animal_sale_asked = animal;
    }

    std::optional<units::Money> sell_animal(ParkScene& scene, const ui::InterfaceAssets& assets, entt::entity animal)
    {
        const auto* sold = scene.sim.registry.valid(animal) ? scene.sim.registry.try_get<simulation::Animal>(animal) : nullptr;
        const std::optional<units::Money> price = sold != nullptr ? animal_buy_price(scene, *sold) : std::nullopt;
        const std::optional<units::Money> got = price ? simulation::sell_animal(scene.sim.registry, animal, *price, scene.park, scene.park_model.finance, scene.ledger_month) : std::nullopt;
        if (!got)
            return std::nullopt;
        if (scene.shown_animal == animal)
        {
            if (auto info = scene.desktop->root().find_as<ui::screens::info::InfoPanel>(kInfoName))
                info->hide();
            scene.shown_animal.reset();
        }
        scene.challenge_notes.emplace_back(assets.strings->get(kSoldText));
        return got;
    }

} // namespace rct3::scene::park
